// test_powershellexec.cpp
//
// PowerShellExec の純粋ロジックと、（pwsh が入っていれば）§18 テンポラリ方式の
// 実往復を FileMaker 無しで検証するスタンドアロンテスト。
//   macOS/Linux : c++ -std=c++17 -I../Source ../Source/PowerShellExec.cpp ../Source/ProcessRun.cpp ../Source/ShellExec.cpp test_powershellexec.cpp -o t && ./t
//
// Part of ZooPlug. License: see License.txt

#include "PowerShellExec.h"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void check_eq(const std::string& actual, const std::string& expected, const char* label) {
    if (actual == expected) {
        std::printf("  [PASS] %s\n", label);
    } else {
        std::printf("  [FAIL] %s\n         expected=[%s]\n         actual  =[%s]\n",
                    label, expected.c_str(), actual.c_str());
        ++g_failures;
    }
}

void check_true(bool cond, const char* label) {
    if (cond) {
        std::printf("  [PASS] %s\n", label);
    } else {
        std::printf("  [FAIL] %s\n", label);
        ++g_failures;
    }
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

std::string to_hex(const std::string& s) {
    static const char* h = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        out.push_back(h[c >> 4]);
        out.push_back(h[c & 0xF]);
        out.push_back(' ');
    }
    if (!out.empty()) out.pop_back();
    return out;
}

} // namespace

int main() {
    using namespace zoo;

    std::printf("PsSingleQuoteEscape:\n");
    check_eq(PsSingleQuoteEscape("abc"), "abc", "no quote unchanged");
    check_eq(PsSingleQuoteEscape("it's"), "it''s", "single quote doubled");
    check_eq(PsSingleQuoteEscape("''"), "''''", "two quotes doubled");

    std::printf("WithUtf8Bom / StripUtf8Bom:\n");
    const std::string bom = WithUtf8Bom("hi");
    check_true(bom.size() == 5 &&
               static_cast<unsigned char>(bom[0]) == 0xEF &&
               static_cast<unsigned char>(bom[1]) == 0xBB &&
               static_cast<unsigned char>(bom[2]) == 0xBF, "WithUtf8Bom prepends EF BB BF");
    check_eq(StripUtf8Bom(bom), "hi", "StripUtf8Bom removes BOM");
    check_eq(StripUtf8Bom("hi"), "hi", "StripUtf8Bom no-op without BOM");
    check_eq(StripUtf8Bom(WithUtf8Bom(u8"表予能")), u8"表予能", "BOM roundtrip keeps Japanese");

    std::printf("MakeStem:\n");
    check_eq(MakeStem(1234, 7), "zoo_ps_1234_7", "stem format");
    check_true(MakeStem(1, 0) != MakeStem(1, 1), "stem unique per counter");

    std::printf("BuildWrappedScript (CLM-safe wrapper):\n");
    const std::string w = BuildWrappedScript("Write-Output 'x'", "C:\\ProgramData\\ZooPlug\\scripts\\o.txt");
    check_true(contains(w, "$ErrorActionPreference = 'Continue'"), "sets ErrorActionPreference");
    check_true(contains(w, "& {"), "opens script block");
    check_true(contains(w, "Write-Output 'x'"), "embeds user command verbatim");
    check_true(contains(w, "2>&1 | Out-File -FilePath '"), "merges stderr and pipes to Out-File");
    check_true(contains(w, "-Encoding utf8"), "Out-File uses utf8");
    // CLM-safe: 生成スクリプトに .NET 型アクセスを含めない（§22）
    check_true(!contains(w, "[Console]") && !contains(w, "[Convert]") &&
               !contains(w, "[Text.Encoding]") && !contains(w, "Add-Type"),
               "wrapper is CLM-safe (no .NET type access)");
    // out path の単一引用符エスケープ
    const std::string w2 = BuildWrappedScript("x", "C:\\a'b\\o.txt");
    check_true(contains(w2, "'C:\\a''b\\o.txt'"), "out path single-quote escaped");

    // ---- 実 pwsh 往復（pwsh が無ければ SKIP）----
    std::printf("RunPowerShell (live pwsh):\n");
    const std::string probe = RunPowerShell("Write-Output 'zoo_probe'");
    if (probe != "zoo_probe") {
        std::printf("  [SKIP] pwsh が見つからない/起動できないため live テストを省略 (probe=[%s])\n", probe.c_str());
    } else {
        check_eq(RunPowerShell("Write-Output 'zoo_ps_ok'"), "zoo_ps_ok", "basic output, trailing newline trimmed");

        // 日本語 UTF-8 往復（§13/§18 の検証法と同じ。16進も出す）
        const std::string jp = RunPowerShell(u8"Write-Output '表予能'");
        std::printf("       japanese -> [%s]  hex=[%s]\n", jp.c_str(), to_hex(jp).c_str());
        check_eq(jp, u8"表予能", "Japanese round-trips as correct UTF-8");

        // 複数行スクリプト（-File 渡しなのでクオート/改行で壊れない）
        check_eq(RunPowerShell("$a = 2\n$b = 3\nWrite-Output ($a * $b)"), "6", "multi-line script");

        // 引用符・$ を含むスクリプトが素通りする（base64 不要・-File で安全）
        check_eq(RunPowerShell("Write-Output 'it''s $tax free'"), "it's $tax free", "quotes and $ pass through verbatim");

        // 2 行出力は CR で連結（NormalizeNewlines）
        check_eq(RunPowerShell("Write-Output 'a'; Write-Output 'b'"), "a\rb", "two lines joined by CR");

        // stderr も 2>&1 で結合捕捉される
        check_true(contains(RunPowerShell("Write-Error 'boom'"), "boom"), "stderr (Write-Error) captured via 2>&1");

        // 空コマンドは空を返す
        check_eq(RunPowerShell(""), "", "empty command returns empty");
    }

#if defined(_WIN32)
    // ---- Windows のホスト選択（bCore）。5.1=v5 / pwsh7=v7 でどちらが起動したか実証 ----
    // pwsh7 が無ければ（ResolvePwshPath が起動できなければ）空が返るので SKIP。
    std::printf("RunPowerShell host selection (Windows, bCore):\n");
    {
        PowerShellOptions ps51;            // bCore=false -> Windows PowerShell 5.1
        const std::string v51 = RunPowerShell("Write-Output ('v' + $PSVersionTable.PSVersion.Major)", ps51);
        std::printf("       bCore=false -> [%s]\n", v51.c_str());
        check_eq(v51, "v5", "bCore=false launches Windows PowerShell 5.1");

        PowerShellOptions core; core.use_core = true;   // bCore=true -> PowerShell 7 (pwsh)
        const std::string v7 = RunPowerShell("Write-Output ('v' + $PSVersionTable.PSVersion.Major)", core);
        if (v7.empty()) {
            std::printf("  [SKIP] pwsh7 が見つからない/起動できないため bCore=true テストを省略\n");
        } else {
            std::printf("       bCore=true  -> [%s]\n", v7.c_str());
            check_eq(v7, "v7", "bCore=true launches PowerShell 7 (pwsh)");

            const std::string jp7 = RunPowerShell(u8"Write-Output '表予能'", core);
            std::printf("       pwsh7 japanese -> [%s] hex=[%s]\n", jp7.c_str(), to_hex(jp7).c_str());
            check_eq(jp7, u8"表予能", "pwsh7 Japanese round-trips as correct UTF-8");

            // pwsh7 でも複数行・クオートが -File で壊れない
            check_eq(RunPowerShell("$a = 4\n$b = 5\nWrite-Output ($a * $b)", core), "20", "pwsh7 multi-line script");
        }
    }
#endif

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

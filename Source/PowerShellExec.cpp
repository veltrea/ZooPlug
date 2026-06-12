// PowerShellExec.cpp — RunPowerShell（§18 テンポラリファイル方式）と純粋ヘルパーの実装
// Part of ZooPlug. License: see License.txt
//
// 設計の正当性は docs/zoo-powershell-design.md §18（テンポラリ方式）・§21/§22（CLM 生存を実機実証）。
// 生成する temp.ps1 は **CLM-safe を厳守**する（cmdlet と文字列演算のみ。.NET 型アクセス
// [Console]/[Convert]/[Text.Encoding]/Add-Type は絶対に入れない）。WDAC enforce 下で
// PowerShell が ConstrainedLanguage に落ちても、この方式なら UTF-8 往復が壊れないことを実証済み。

#include "PowerShellExec.h"

#include "ProcessRun.h"
#include "ShellExec.h"   // zoo::NormalizeNewlines を再利用

#include <atomic>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <unistd.h>
  #include <cstdlib>
  #include <cstdio>
#endif

namespace zoo {

// ============================ 純粋ロジック ============================

std::string PsSingleQuoteEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        out.push_back(c);
        if (c == '\'') out.push_back('\'');   // PowerShell 単一引用符内では ' を '' にエスケープ
    }
    return out;
}

std::string BuildWrappedScript(const std::string& user_command_utf8,
                               const std::string& out_path) {
    // §18 のラップ。結合出力(stdout+stderr)を Out-File -Encoding utf8 でファイルへ。
    //   $ErrorActionPreference = 'Continue'
    //   & {
    //   <user command>
    //   } 2>&1 | Out-File -FilePath '<out>' -Encoding utf8 -Width 8192
    // すべて cmdlet / 演算子のみ＝ConstrainedLanguage でも動く。-Width は整形出力の折返し対策。
    std::string s;
    s += "$ErrorActionPreference = 'Continue'\r\n";
    s += "& {\r\n";
    s += user_command_utf8;
    s += "\r\n} 2>&1 | Out-File -FilePath '";
    s += PsSingleQuoteEscape(out_path);
    s += "' -Encoding utf8 -Width 8192\r\n";
    return s;
}

std::string WithUtf8Bom(const std::string& text) {
    // 5.1 が .ps1 を CP932 誤読しないよう UTF-8 BOM(EF BB BF) を前置する（§18 要件）。
    std::string out;
    out.reserve(text.size() + 3);
    out.push_back(static_cast<char>(0xEF));
    out.push_back(static_cast<char>(0xBB));
    out.push_back(static_cast<char>(0xBF));
    out.append(text);
    return out;
}

std::string StripUtf8Bom(const std::string& text) {
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        return text.substr(3);
    }
    return text;
}

std::string MakeStem(unsigned long pid, unsigned long long counter) {
    return "zoo_ps_" + std::to_string(pid) + "_" + std::to_string(counter);
}

// ============================ プラットフォーム I/O ============================

namespace {

std::atomic<unsigned long long> g_counter{0};

bool WriteAllBytes(const std::string& path, const std::string& data) {
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

bool ReadAllBytes(const std::string& path, std::string& out) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

void RemoveFileQuiet(const std::string& path) {
    std::remove(path.c_str());
}

#if defined(_WIN32)

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

// §19: 生成スクリプト/出力は必ず %PROGRAMDATA%\ZooPlug\scripts\ に置く（AppLocker 許可登録の的）。
// パスは ASCII なので narrow の std::ofstream/ifstream でそのまま扱える。
std::string ScriptDir() {
    char buf[MAX_PATH];
    const DWORD n = ::GetEnvironmentVariableA("PROGRAMDATA", buf, MAX_PATH);
    std::string base = (n > 0 && n < MAX_PATH) ? std::string(buf, n) : std::string("C:\\ProgramData");
    const std::string zoo = base + "\\ZooPlug";
    const std::string scr = zoo + "\\scripts";
    ::CreateDirectoryA(zoo.c_str(), nullptr);
    ::CreateDirectoryA(scr.c_str(), nullptr);
    return scr;
}

unsigned long CurrentPid() { return static_cast<unsigned long>(::GetCurrentProcessId()); }

// §18: pwsh は実体パスで解決する。MSIX エイリアス（WindowsApps\pwsh.exe）は
// CreateProcessW で起動不能(gle=15612)なので使わない。
std::string ResolvePwshPath() {
    const char* candidates[] = {
        "C:\\Program Files\\PowerShell\\7\\pwsh.exe",
        "C:\\Program Files\\PowerShell\\7-preview\\pwsh.exe",
    };
    for (const char* c : candidates) {
        if (::GetFileAttributesA(c) != INVALID_FILE_ATTRIBUTES) return c;
    }
    return "pwsh.exe"; // 最後の手段（PATH 解決。MSIX エイリアスに当たると起動できない点に注意）
}

const char* kSep = "\\";

#else  // POSIX

// mac/linux は %PROGRAMDATA% が無いので $TMPDIR(無ければ /tmp) 配下に置く。
std::string ScriptDir() {
    const char* tmp = ::getenv("TMPDIR");
    std::string base = (tmp && *tmp) ? std::string(tmp) : std::string("/tmp");
    if (!base.empty() && base.back() == '/') base.pop_back();
    const std::string zoo = base + "/ZooPlug";
    const std::string scr = zoo + "/scripts";
    ::mkdir(zoo.c_str(), 0700);
    ::mkdir(scr.c_str(), 0700);
    return scr;
}

unsigned long CurrentPid() { return static_cast<unsigned long>(::getpid()); }

const char* kSep = "/";

#endif

} // namespace

std::string RunPowerShell(const std::string& command_utf8, const PowerShellOptions& opts) {
    if (command_utf8.empty()) return std::string();

    const std::string dir  = ScriptDir();
    const std::string stem = MakeStem(CurrentPid(), g_counter.fetch_add(1));
    const std::string ps1  = dir + kSep + stem + ".ps1";
    const std::string out  = dir + kSep + stem + ".out.txt";

    // §18: temp.ps1 は UTF-8 BOM 付きで書く（5.1 の CP932 誤読対策）。
    const std::string wrapped = WithUtf8Bom(BuildWrappedScript(command_utf8, out));
    if (!WriteAllBytes(ps1, wrapped)) {
        return std::string();
    }

    ProcessResult pr;
#if defined(_WIN32)
    const std::string host = opts.use_core ? ResolvePwshPath() : std::string("powershell.exe");
    const std::wstring cmd =
        L"\"" + Utf8ToWide(host) + L"\""
        L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + Utf8ToWide(ps1) + L"\"";
    pr = RunProcessCommandLine(cmd);
#else
    (void)opts; // mac/linux に 5.1 は無いので bCore に関わらず pwsh を使う
    const std::vector<std::string> argv = {
        "pwsh", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", ps1
    };
    pr = RunProcessArgv(argv);
#endif

    std::string result;
    if (pr.started) {
        std::string raw;
        if (ReadAllBytes(out, raw)) {
            // out.txt は Out-File -Encoding utf8 が書いた UTF-8。5.1 は BOM 付きなので除去。
            result = NormalizeNewlines(StripUtf8Bom(raw));
        }
    }

    // temp は一意名＋後始末（並行呼び出し対策）
    RemoveFileQuiet(ps1);
    RemoveFileQuiet(out);
    return result;
}

} // namespace zoo

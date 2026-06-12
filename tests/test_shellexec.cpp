// test_shellexec.cpp
//
// ShellExec の純粋ロジックを FileMaker 無しで検証するスタンドアロンテスト。
//   macOS / Linux : c++ -std=c++17 -I../Source ../Source/ShellExec.cpp test_shellexec.cpp -o test && ./test
//   Windows (MSVC): cl /std:c++17 /EHsc /I..\Source ..\Source\ShellExec.cpp test_shellexec.cpp
//
// Part of ZooPlug. License: see License.txt

#include "ShellExec.h"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void check_eq(const std::string& actual, const std::string& expected, const char* label) {
    const bool ok = (actual == expected);
    if (ok) {
        std::printf("  [PASS] %s\n", label);
    } else {
        std::printf("  [FAIL] %s\n         expected=[%s]\n         actual  =[%s]\n",
                    label, expected.c_str(), actual.c_str());
        ++g_failures;
    }
}

} // namespace

int main() {
    using namespace zoo;

    std::printf("NormalizeNewlines:\n");
    check_eq(NormalizeNewlines("a\r\nb"), "a\rb", "CRLF -> CR");
    check_eq(NormalizeNewlines("a\nb"), "a\rb", "lone LF -> CR");
    check_eq(NormalizeNewlines("a\rb"), "a\rb", "CR stays CR");
    check_eq(NormalizeNewlines("a\r\n\r\n"), "a", "trailing newlines trimmed");
    check_eq(NormalizeNewlines("abc"), "abc", "no newline unchanged");
    check_eq(NormalizeNewlines(""), "", "empty string");

    std::printf("RunShellCommand:\n");

    const std::string echoed = RunShellCommand("echo zoo_ok");
    std::printf("       echo zoo_ok -> [%s]\n", echoed.c_str());
    check_eq(echoed, "zoo_ok", "echo output, trailing newline trimmed");

#if defined(_WIN32)
    const std::string multi = RunShellCommand("echo a& echo b");
#else
    const std::string multi = RunShellCommand("printf 'a\\nb\\n'");
#endif
    std::printf("       two lines  -> [%s]\n", multi.c_str());
    check_eq(multi, "a\rb", "multi-line joined by CR");

#if defined(_WIN32)
    // 日本語 Windows での文字コード往復: echo で日本語を出力し、コンソールの
    // OEM コードページ(CP932)から UTF-8 へ正しく変換できるか確認する。
    const std::string jp = RunShellCommand(u8"echo 日本語テスト"); // 日本語テスト
    std::printf("       japanese   -> [%s]\n", jp.c_str());
    check_eq(jp, u8"日本語テスト", "Japanese (CP932) decodes to correct UTF-8");
#endif

    check_eq(RunShellCommand(""), "", "empty command returns empty");

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

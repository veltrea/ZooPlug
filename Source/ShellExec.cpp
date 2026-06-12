// ShellExec.cpp — RunShellCommand / NormalizeNewlines の実装
// Part of ZooPlug. License: see License.txt
//
// 実プロセス起動は ProcessRun に委譲し、ここでは「コマンドラインの組み立て」と
// 「出力バイトのデコード（Windows は OEM/CP932 → UTF-8）＋改行正規化」だけを行う。
// zoo_powershell（PowerShellExec）と CreateProcessW/POSIX 起動コードを共有する。

#include "ShellExec.h"

#include "ProcessRun.h"

#include <vector>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif

namespace zoo {

std::string NormalizeNewlines(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    // CRLF→CR, 単独 LF→CR に正規化（FileMaker の内部改行は CR）
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            out.push_back('\r');
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i; // CRLF をまとめて 1 つの CR にする
            }
        } else if (c == '\n') {
            out.push_back('\r');
        } else {
            out.push_back(c);
        }
    }

    // 末尾の改行を取り除く（echo 等が付ける最後の改行を消す）
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) {
        out.pop_back();
    }
    return out;
}

#if defined(_WIN32)

// ---- Windows 実装：cmd.exe を起動し、出力を OEM コードページ(CP932)として復号 ----

namespace {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

// 指定コードページのバイト列 → UTF-16
std::wstring BytesToWide(const std::string& s, UINT codepage) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(codepage, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(codepage, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

// UTF-16 → UTF-8
std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

} // namespace

std::string RunShellCommand(const std::string& command_utf8) {
    if (command_utf8.empty()) return std::string();

    // cmd.exe /S /C "<command>"
    //   /S /C : 先頭と末尾の " を 1 組だけ外し、間をそのまま 1 コマンドとして実行する。
    //           ユーザーのワンライナーをそのまま渡せる。
    const std::wstring command_line = L"cmd.exe /S /C \"" + Utf8ToWide(command_utf8) + L"\"";

    const ProcessResult pr = RunProcessCommandLine(command_line);
    if (!pr.started) return std::string();

    // コンソール OEM コードページ（日本語環境なら CP932）→ UTF-16 → UTF-8
    const std::string utf8 = WideToUtf8(BytesToWide(pr.bytes, CP_OEMCP));
    return NormalizeNewlines(utf8);
}

#else

// ---- POSIX 実装（macOS / Linux）：/bin/sh -c をシェル経由で実行 ----

std::string RunShellCommand(const std::string& command_utf8) {
    if (command_utf8.empty()) return std::string();

    // stderr は ProcessRun が stdout と同じパイプに束ねる（" 2>&1" 相当）
    const std::vector<std::string> argv = { "/bin/sh", "-c", command_utf8 };
    const ProcessResult pr = RunProcessArgv(argv);
    if (!pr.started) return std::string();

    // macOS / Linux のシェル出力は通常 UTF-8 なのでそのまま正規化のみ行う
    return NormalizeNewlines(pr.bytes);
}

#endif

} // namespace zoo

// ShellExec.cpp — RunShellCommand / NormalizeNewlines の実装
// Part of ZooPlug. License: see License.txt

#include "ShellExec.h"

#include <vector>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <cstdio>
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

// ---- Windows 実装：cmd.exe をパイプで起動して出力を取得 ----

namespace {

// UTF-8 → UTF-16
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

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    // 子プロセスの標準出力／標準エラーを受け取るパイプ
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!::CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        return std::string();
    }
    // 読み取り側は子に継承させない
    ::SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    // 標準入力は NUL に向け、入力待ちでハングしないようにする
    HANDLE nul_in = ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si;
    ::ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul_in;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe; // stderr もまとめて取得

    PROCESS_INFORMATION pi;
    ::ZeroMemory(&pi, sizeof(pi));

    // cmd.exe /S /C "<command>"
    //   /S /C : 先頭と末尾の " を 1 組だけ外し、間をそのまま 1 コマンドとして実行する。
    //           ユーザーのワンライナーをそのまま渡せる。
    std::wstring command_line = L"cmd.exe /S /C \"" + Utf8ToWide(command_utf8) + L"\"";
    std::vector<wchar_t> mutable_cmd(command_line.begin(), command_line.end());
    mutable_cmd.push_back(L'\0'); // CreateProcessW は書き換え可能なバッファを要求する

    const BOOL ok = ::CreateProcessW(
        nullptr,            // アプリ名はコマンドラインから解決
        mutable_cmd.data(),
        nullptr, nullptr,
        TRUE,               // パイプを継承させる
        CREATE_NO_WINDOW,   // コンソール窓を出さない
        nullptr, nullptr,
        &si, &pi);

    // 親側では書き込み側を閉じる（子だけが保持 → 子の終了で EOF を検出できる）
    ::CloseHandle(write_pipe);
    if (nul_in) {
        ::CloseHandle(nul_in);
    }

    if (!ok) {
        ::CloseHandle(read_pipe);
        return std::string();
    }

    // EOF まで生バイトを読む。読みながら受けるのでパイプ満杯でのデッドロックは起きない。
    std::string raw;
    char buf[4096];
    DWORD nread = 0;
    while (::ReadFile(read_pipe, buf, sizeof(buf), &nread, nullptr) && nread > 0) {
        raw.append(buf, nread);
    }
    ::CloseHandle(read_pipe);

    ::WaitForSingleObject(pi.hProcess, INFINITE);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);

    // コンソール OEM コードページ（日本語環境なら CP932）→ UTF-16 → UTF-8
    const std::string utf8 = WideToUtf8(BytesToWide(raw, CP_OEMCP));
    return NormalizeNewlines(utf8);
}

#else

// ---- POSIX 実装（macOS / Linux）：popen で /bin/sh -c 相当 ----

std::string RunShellCommand(const std::string& command_utf8) {
    if (command_utf8.empty()) return std::string();

    // stderr もまとめて取得するため 2>&1 を付ける
    const std::string full = command_utf8 + " 2>&1";
    FILE* pipe = ::popen(full.c_str(), "r");
    if (!pipe) return std::string();

    std::string raw;
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), pipe)) > 0) {
        raw.append(buf, n);
    }
    ::pclose(pipe);

    // macOS / Linux のシェル出力は通常 UTF-8 なのでそのまま正規化のみ行う
    return NormalizeNewlines(raw);
}

#endif

} // namespace zoo

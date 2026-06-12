// ProcessRun.cpp — RunProcessCommandLine (Windows) / RunProcessArgv (POSIX)
// Part of ZooPlug. License: see License.txt

#include "ProcessRun.h"

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <vector>
#else
  #include <spawn.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/wait.h>
  #include <cstring>
  #include <cerrno>
  extern char** environ;
#endif

namespace zoo {

#if defined(_WIN32)

ProcessResult RunProcessCommandLine(const std::wstring& command_line) {
    ProcessResult result;
    if (command_line.empty()) {
        return result;
    }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    // 子プロセスの標準出力／標準エラーを受け取る匿名パイプ
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!::CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        return result;
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
        return result; // started=false
    }
    result.started = true;

    // EOF まで生バイトを読む。読みながら受けるのでパイプ満杯でのデッドロックは起きない。
    char buf[4096];
    DWORD nread = 0;
    while (::ReadFile(read_pipe, buf, sizeof(buf), &nread, nullptr) && nread > 0) {
        result.bytes.append(buf, nread);
    }
    ::CloseHandle(read_pipe);

    ::WaitForSingleObject(pi.hProcess, INFINITE);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);

    return result;
}

#else

ProcessResult RunProcessArgv(const std::vector<std::string>& argv) {
    ProcessResult result;
    if (argv.empty() || argv[0].empty()) {
        return result;
    }

    int pipefd[2];
    if (::pipe(pipefd) != 0) {
        return result;
    }
    const int rd = pipefd[0];
    const int wr = pipefd[1];

    // 子プロセスの stdout/stderr をパイプの書き込み側へ、stdin を /dev/null へ。
    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    ::posix_spawn_file_actions_adddup2(&actions, wr, STDOUT_FILENO);
    ::posix_spawn_file_actions_adddup2(&actions, wr, STDERR_FILENO);
    ::posix_spawn_file_actions_addclose(&actions, rd);
    ::posix_spawn_file_actions_addclose(&actions, wr);

    // execvp 相当の引数配列（末尾 nullptr）
    std::vector<char*> c_argv;
    c_argv.reserve(argv.size() + 1);
    for (const std::string& a : argv) {
        c_argv.push_back(const_cast<char*>(a.c_str()));
    }
    c_argv.push_back(nullptr);

    pid_t pid = 0;
    // posix_spawnp は argv[0] を PATH から解決する。見つからなければ非 0 を返す。
    const int spawn_rc = ::posix_spawnp(&pid, c_argv[0], &actions, nullptr, c_argv.data(), environ);
    ::posix_spawn_file_actions_destroy(&actions);
    ::close(wr); // 親は書き込み側を閉じる（子の終了で EOF を検出）

    if (spawn_rc != 0) {
        ::close(rd);
        return result; // started=false
    }
    result.started = true;

    char buf[4096];
    ssize_t n = 0;
    while ((n = ::read(rd, buf, sizeof(buf))) > 0) {
        result.bytes.append(buf, static_cast<std::size_t>(n));
    }
    ::close(rd);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        // シグナル割り込みは無視して待ち直す
    }

    return result;
}

#endif

} // namespace zoo

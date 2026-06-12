// ProcessRun.h
//
// 子プロセスを起動して標準出力＋標準エラーを「生バイト」で捕捉する低レベル層。
// moo_shell（ShellExec）と zoo_powershell（PowerShellExec）が共有する。
// FileMaker SDK (FMWrapper) には一切依存しないので、FileMaker を起動せずに
// 単体でビルド・実行・テストできる。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_PROCESS_RUN_H
#define ZOO_PROCESS_RUN_H

#include <string>
#include <vector>

namespace zoo {

// 子プロセスの起動結果。
//   started : プロセスを起動できたか（実行ファイルが見つからない等は false）
//   bytes   : 標準出力＋標準エラーを結合した「生バイト」（エンコード変換なし）
struct ProcessResult {
    bool started = false;
    std::string bytes;
};

#if defined(_WIN32)

// Windows: フルコマンドライン文字列を CreateProcessW で起動する。
//   - 引用やエスケープは呼び出し側の責任（lpCommandLine をそのまま使う）。
//   - CREATE_NO_WINDOW でコンソール窓を出さない。標準入力は NUL に向ける。
//   - 標準出力と標準エラーを 1 本の匿名パイプにまとめて EOF まで読み、終了を待つ。
ProcessResult RunProcessCommandLine(const std::wstring& command_line);

#else

// POSIX (macOS / Linux): argv を「シェルを介さず」直接起動する（execvp 相当）。
//   - argv[0] が実行ファイル名（PATH 解決される）。引用問題が起きない。
//   - 標準出力と標準エラーを 1 本のパイプにまとめて EOF まで読み、終了を待つ。
//   - 標準入力は /dev/null に向ける。
// シェルのワンライナーを実行したい場合は argv = {"/bin/sh", "-c", command}。
ProcessResult RunProcessArgv(const std::vector<std::string>& argv);

#endif

} // namespace zoo

#endif // ZOO_PROCESS_RUN_H

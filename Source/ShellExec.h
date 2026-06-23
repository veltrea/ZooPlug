// ShellExec.h
//
// シェルのワンライナーを実行して出力を返す純粋ロジック。
// FileMaker SDK (FMWrapper) に一切依存しないので、FileMaker を起動せずに
// 単体でビルド・実行・テストできる（tests/test_shellexec.cpp 参照）。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_SHELL_EXEC_H
#define ZOO_SHELL_EXEC_H

#include <string>

namespace zoo {

// コマンドをワンライナーとしてシェルで実行し、標準出力＋標準エラーを
// UTF-8 文字列で返す。改行は CR (\r) に正規化し、末尾の改行は取り除く。
//
//   Windows      : cmd.exe /S /C "<command>"
//                  （出力はコンソールの OEM コードページ。日本語環境なら
//                    CP932/Shift_JIS として解釈し UTF-8 に変換する）
//   macOS / Linux: /bin/sh -c "<command>"（出力は UTF-8 とみなす）
//
// command_utf8 は UTF-8。空文字列、または起動に失敗した場合は空を返す。
std::string RunShellCommand(const std::string& command_utf8);

// 改行を CR(\r) に正規化し（CRLF→CR, 単独 LF→CR）、末尾の改行を除去する。
// RunShellCommand の内部で使うが、単体テストしやすいよう公開している。
std::string NormalizeNewlines(const std::string& text);

} // namespace zoo

#endif // ZOO_SHELL_EXEC_H

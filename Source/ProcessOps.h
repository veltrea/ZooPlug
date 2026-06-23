// ProcessOps.h
//
// MooPlug の Process 4 関数の実装。
//   Moo_ProcessCount / Moo_ProcessRunning / Moo_ProcessList / Moo_ProcessKill
// FileMaker SDK (FMWrapper) に一切依存しないので、FileMaker を起動せずに
// 単体でビルド・実行・テストできる（tests/test_processops.cpp 参照）。
//
// プロセス列挙はプラットフォームで分岐する（ProcessOps.cpp）:
//   Windows : CreateToolhelp32Snapshot + Process32NextW（イメージ名 "name.exe"）
//   macOS   : proc_listpids(PROC_ALL_PIDS) + proc_name
//   Linux   : /proc/<pid>/comm
// 名前マッチ・件数集計・区切り連結など列挙後のロジックは共通（テスト対象）。
//
// 各関数は MooPlug のエラー番号（Moo_関数名|Err_N の N）を int で返す。0 = 成功。
// 引数不足（Err_1）は呼び出し側グルー（ZooPlug.cpp）で弾く。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_PROCESS_OPS_H
#define ZOO_PROCESS_OPS_H

#include <string>
#include <vector>

namespace zoo {

struct ProcInfo {
    unsigned long pid = 0;
    std::string name;   // 実行イメージ名（Windows は "name.exe"）
};

// 実行中プロセスを列挙する（プラットフォーム依存）。失敗時は空。
std::vector<ProcInfo> EnumerateProcesses();

// プロセス名がクエリに一致するか。大小無視で、Windows の ".exe" 拡張子の
// 有無を吸収する（"notepad" と "notepad.exe" は一致）。テスト用に公開。
bool ProcessNameMatches(const std::string& proc_name, const std::string& query);

// Moo_ProcessCount( sProcess ) — sProcess が空なら全プロセス数、非空なら一致数を返す。
//   count_out に件数。エラーは Err_1（引数数・グルー側）のみ。
int ProcessCount(const std::string& name_utf8, long& count_out);

// Moo_ProcessRunning( sProcess ) — 一致するプロセスが有るか。running_out に true/false。
//   2 = 入力が空（"Invalid input process."）
int ProcessRunning(const std::string& name_utf8, bool& running_out);

// Moo_ProcessList( {sSeparator} ) — プロセス名の一覧を separator で連結。
//   separator 既定は呼び出し側で "|"（PrinterList と揃える。TODO-compat: 実機既定未確認）。
//   list_out に結果。失敗しても基本 0（列挙が空なら空文字列）。
int ProcessList(const std::string& separator_utf8, std::string& list_out);

// Moo_ProcessKill( sProcess ) — 一致するプロセスをすべて終了させる。
//   2 = 入力が空 / 終了に失敗（"Error terminating process."）
int ProcessKill(const std::string& name_utf8);

} // namespace zoo

#endif // ZOO_PROCESS_OPS_H

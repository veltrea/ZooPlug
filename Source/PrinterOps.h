// PrinterOps.h
//
// MooPlug の Printer 2 関数の実装。
//   Moo_PrinterDefault（取得/設定） / Moo_PrinterList
// FileMaker SDK (FMWrapper) に一切依存しない（tests/test_printerops.cpp 参照）。
//
// プラットフォーム分岐（PrinterOps.cpp）:
//   Windows : winspool（GetDefaultPrinter / SetDefaultPrinter / EnumPrinters）
//   macOS/Linux : CUPS（cupsGetNamedDest / cupsGetDests / cupsSetDests）
//
// 各関数は MooPlug のエラー番号（Moo_関数名|Err_N の N）を int で返す。0 = 成功。
// 引数不足（Err_1）は呼び出し側グルー（ZooPlug.cpp）で弾く。
// 注: MooPlug の注記どおり「FileMaker の印刷には影響しない」（システム既定の取得/設定）。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_PRINTER_OPS_H
#define ZOO_PRINTER_OPS_H

#include <string>

namespace zoo {

// Moo_PrinterDefault()（引数なし）: システム既定プリンタ名を name_out に返す。
//   2 = 取得失敗（既定プリンタが無い場合も含む）
int PrinterGetDefault(std::string& name_out);

// Moo_PrinterDefault( sPrinter ): 既定プリンタを設定する。
//   3 = 設定失敗（指定プリンタが見つからない場合も含む）
int PrinterSetDefault(const std::string& name_utf8);

// Moo_PrinterList( {sSeparator} ): インストール済みプリンタ名を separator で連結。
//   既定 separator は呼び出し側で "|"。
//   2 = 一覧取得失敗 / 3 = プリンタが 1 台も無い
int PrinterList(const std::string& separator_utf8, std::string& list_out);

} // namespace zoo

#endif // ZOO_PRINTER_OPS_H

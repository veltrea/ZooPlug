// DialogOps.h
//
// MooPlug の Dialog 3 関数の実装。
//   Moo_DialogColour / Moo_DialogFile / Moo_DialogFolder
// FileMaker SDK (FMWrapper) には依存しないが、これらは GUI モーダルダイアログなので
// ヘッドレスの単体テストはできない（実機確認は FileMaker Pro（実機））。
//
// プラットフォーム分岐:
//   Windows : comdlg32（ChooseColor / GetOpenFileName / GetSaveFileName）+
//             shell32（SHBrowseForFolder）   → DialogOps.cpp
//   macOS   : AppKit（NSColorPanel / NSOpenPanel / NSSavePanel）→ DialogOps_mac.mm
//   Linux   : GUI 無し（FileMaker Server）→ すべて Err_2 を返す（DialogOps.cpp）
//
// 各関数は MooPlug のエラー番号（Moo_関数名|Err_N の N）を int で返す。0 = 成功。
// 引数不足（Err_1）は呼び出し側グルー（ZooPlug.cpp）で弾く。
//
// 注意（UI スレッド）: プラグイン関数は FileMaker の計算スレッドで同期実行される。
// FileMaker Pro ではこれが実質メイン UI スレッドなのでモーダルを出せる。macOS の AppKit は
// メインスレッド制約があるため、メインスレッド以外から呼ばれた場合は dispatch_sync で
// メインに載せる（DialogOps_mac.mm）。Server/WebDirect では意味をなさないので
// グルー側で kServerCompatible を付けない。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_DIALOG_OPS_H
#define ZOO_DIALOG_OPS_H

#include <string>

namespace zoo {

// Moo_DialogColour( {bFull} ): カラーピッカーを出し、選択色を colour_out に返す。
//   full=true でフルピッカー（Windows の CC_FULLOPEN 相当）。
//   戻り形式は 6 桁大文字 16 進 "RRGGBB"（TODO-compat: 実 0.4.9 の形式は要実機照合。
//   "R,G,B" 十進や先頭 '#' の可能性あり）。
//   2 = ユーザーがキャンセル
int DialogColour(bool full, std::string& colour_out);

// Moo_DialogFile( {bOpen ; sTitle ; sDefault} ): ファイル選択ダイアログ。
//   open=true で「開く」、false で「保存」。title はダイアログタイトル、
//   default_name は既定ファイル名。選択パスを path_out に返す。
//   2 = ユーザーがキャンセル
int DialogFile(bool open, const std::string& title, const std::string& default_name,
               std::string& path_out);

// Moo_DialogFolder( {sTitle ; bNewFolder} ): フォルダ選択ダイアログ。
//   title はタイトル、new_folder=true で「新規フォルダ」ボタンを出す。
//   選択パスを path_out に返す。
//   2 = ユーザーがキャンセル / 3 = 選択パスの取得に失敗
int DialogFolder(const std::string& title, bool new_folder, std::string& path_out);

} // namespace zoo

#endif // ZOO_DIALOG_OPS_H

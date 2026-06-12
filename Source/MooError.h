// MooError.h
//
// MooPlug 互換のエラー文字列の生成と、Moo_ErrorDetail のコード→説明文マップ。
// FileMaker SDK (FMWrapper) に一切依存しないので、FileMaker を起動せずに
// 単体でビルド・実行・テストできる（tests/test_mooerror.cpp 参照）。
//
// MooPlug はエラーを「戻り値テキスト」として `Moo_関数名|Err_N` の形式で返す
// （アンダースコアあり。実バイナリ 0.4.9 で確定。docs/mooplug-reference.md 参照）。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_MOO_ERROR_H
#define ZOO_MOO_ERROR_H

#include <string>

namespace zoo {

// Moo_Version が返す文字列。
// 0.4.9 実バイナリの文字列プールで Moo_ErrorDetail チェーン直後に置かれた
// ASCII リテラル "MooPlug 0.4.9" が唯一の候補（"0.4.9" 単体はバージョン
// リソースにしか存在しない）。TODO-compat: WORK1 の FM11 + 実 0.4.9 で
// データビューア評価して確定する。
extern const char* const kMooVersionString;

// "Moo_FileCopy" + 3 → "Moo_FileCopy|Err_3"
std::string MakeMooError(const std::string& function_name, int error_number);

// Moo_ErrorDetail の実装。"Moo_FileCopy|Err_3" → "Source file does not exist."
// マップは 0.4.9 実バイナリの文字列プール（Moo_ErrorDetail の if/else チェーン領域
// 449260〜453536）から復元した。ドキュメントと食い違う場合はバイナリを正とした
// （例: Moo_DownloadFile|Err_4 / Moo_ZipCompress|Err_4 / Moo_FolderList|Err_4,5）。
// 未知のコードには空文字列を返す（TODO-compat: 実機の未知コード時の挙動は未確認）。
std::string MooErrorDetail(const std::string& error_code);

} // namespace zoo

#endif // ZOO_MOO_ERROR_H

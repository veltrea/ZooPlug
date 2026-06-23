// MooError.h
//
// MooPlug 互換のエラー文字列の生成と、Moo_ErrorDetail のコード→説明文マップ。
// FileMaker SDK (FMWrapper) に一切依存しないので、FileMaker を起動せずに
// 単体でビルド・実行・テストできる（tests/test_mooerror.cpp 参照）。
//
// MooPlug はエラーを「戻り値テキスト」として `Moo_関数名|Err_N` の形式で返す
// （アンダースコアあり。0.4.9 の観測された戻り値に基づく）。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_MOO_ERROR_H
#define ZOO_MOO_ERROR_H

#include <string>

namespace zoo {

// "Moo_FileCopy" + 3 → "Moo_FileCopy|Err_3"
std::string MakeMooError(const std::string& function_name, int error_number);

// Moo_ErrorDetail の実装。"Moo_FileCopy|Err_3" → "Source file does not exist."
// エラー文字列のマップは MooPlug 0.4.9 の観測された挙動をモデルにしている。
// 未知のコードには空文字列を返す。
std::string MooErrorDetail(const std::string& error_code);

} // namespace zoo

#endif // ZOO_MOO_ERROR_H

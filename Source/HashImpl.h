// HashImpl.h
//
// Moo_Hash の純粋ロジック: MD5 / SHA-1 / SHA-256 / SHA-512。
// 外部ライブラリに依存しない自己完結実装（OpenSSL 等を避けるため自前実装。
// NIST テストベクタで検証 — tests/test_hash.cpp）。
// FileMaker SDK (FMWrapper) に一切依存しない。
//
// 出力は小文字 16 進（実機の戻り値と整合）。
// 戻り値は Moo_Hash のエラー番号: 0=成功 / 2=未知のアルゴリズム / 3=空入力 /
// 4=ファイル無し / 5=生成失敗。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_HASH_IMPL_H
#define ZOO_HASH_IMPL_H

#include <string>

namespace zoo {

// algorithm は "md5" / "sha1" / "sha256" / "sha512"（ASCII の大文字小文字は無視）
int HashString(const std::string& algorithm, const std::string& text, std::string& hex_out);
int HashFile(const std::string& algorithm, const std::string& file_utf8, std::string& hex_out);

} // namespace zoo

#endif // ZOO_HASH_IMPL_H

// Variant.h
// ビルドのアイデンティティ定義 — 製品名・関数プレフィックス・プラグイン ID・
// バージョン文字列・拡張の有無を 1 か所に集約する。
//
// Part of ZooPlug. License: see License.txt
//
#pragma once

#include <string>

namespace zoo {

struct Variant {
	const char* product;         // "ZooPlug" — システム設定のプラグイン一覧に出る名前
	const char* prefix;          // "zoo_"    — 関数名のプレフィックス
	const char* plugin_id;       // "ZooP"    — 4 文字の FileMaker プラグイン ID
	const char* version;         // zoo_Version が返すバージョン文字列
	bool        has_extensions;  // true = zoo_PowerShell 等の独自拡張を登録する
};

constexpr Variant kVariant = { "ZooPlug", "zoo_", "ZooP", "ZooPlug 1.1.1", true };

// "FileCopy" → "zoo_FileCopy" を返す。エラーメッセージ生成で使う
// (c_str() を取れば const char* に渡せる)。
inline std::string FnName ( const char* base_name )
{
	return std::string ( kVariant.prefix ) + base_name;
}

} // namespace zoo

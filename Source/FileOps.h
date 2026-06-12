// FileOps.h
//
// MooPlug の File 7 関数 + Folder 6 関数の純粋ロジック（std::filesystem ベース）。
// FileMaker SDK (FMWrapper) に一切依存しないので、FileMaker を起動せずに
// 単体でビルド・実行・テストできる（tests/test_fileops.cpp 参照）。
//
// 各関数は MooPlug のエラー番号（Moo_関数名|Err_N の N）を int で返す。0 = 成功。
// エラー番号の意味は docs/mooplug-reference.md と Source/MooError.cpp を参照。
//
// パスはすべて UTF-8 で受け取る。Windows では fs::path(utf8) が ANSI(CP932) 解釈で
// 壊れるため、必ず PathFromUTF8（u8path 経由）でパスを作る（skill
// windows-cmd-japanese-encoding §8。「ソ・表・予・能」等のダメ文字対策）。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_FILE_OPS_H
#define ZOO_FILE_OPS_H

#include <cstdint>
#include <filesystem>
#include <string>

namespace zoo {

// UTF-8 文字列 ⇔ fs::path（Windows では UTF-16 経由で変換し CP932 解釈を避ける）
std::filesystem::path PathFromUTF8(const std::string& utf8);
std::string PathToUTF8(const std::filesystem::path& p);

// ---- File 関数（戻り値は Moo エラー番号。0 = 成功） ----

// Moo_FileExists: exists_out に存在有無。エラーは 2（空入力）のみ。
int FileExists(const std::string& file_utf8, bool& exists_out);

// Moo_FileCopy: 2=空 src / 3=src 無し / 4=空 dest / 5=dest 既存(上書き不可) /
//               6=コピー失敗 / 7=既存 dest の削除失敗(上書き時)
int FileCopy(const std::string& src_utf8, const std::string& dest_utf8, bool overwrite);

// Moo_FileDelete: 2=空入力 / 3=削除失敗 / 4=無し
int FileDelete(const std::string& file_utf8);

// Moo_FileMove: エラー番号は FileCopy と同じ並び。デバイス跨ぎは copy+delete で代替。
int FileMove(const std::string& src_utf8, const std::string& dest_utf8, bool overwrite);

// Moo_FileRead: 2=空入力 / 3=オープン失敗 / 5=読み込み失敗。
// text_out は UTF-8（UTF-8 として不正なら Windows では CP932 として復号）、
// BOM 除去・改行 CR 正規化済み。
int FileRead(const std::string& file_utf8, std::string& text_out);

// Moo_FileWrite: 2=空入力 / 3=既存(追記でない) / 4=オープン失敗 / 5=書き込み失敗。
// text_utf8 は UTF-8 のまま書く。改行は OS ネイティブ（Win=CRLF / 他=LF）に変換する。
int FileWrite(const std::string& file_utf8, const std::string& text_utf8, bool append);

// ---- Moo_FileInfo 用 ----

// size: 2=空入力 / 3=無し / 6=取得失敗
int FileSize(const std::string& file_utf8, std::uint64_t& bytes_out);

// Windows の StrFormatByteSize と同じ書式の「人間が読める」サイズ文字列。
//   1024 未満      → "532 bytes"
//   それ以上は KB/MB/GB/TB/PB/EB へ切り捨てスケール、有効 3 桁
//   （10 未満 = 小数 2 桁、100 未満 = 小数 1 桁、以上 = 整数。すべて切り捨て）
std::string HumanReadableSize(std::uint64_t bytes);

// created / modified の取得・設定（ローカル時刻の年月日時分秒で受け渡す）
struct FileTimeParts {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

// creation=true で作成日時、false で更新日時。2=空入力 / 3=無し / 6=取得失敗
// （Linux は作成日時を持たないため creation=true は 6）
int FileTimeGet(const std::string& file_utf8, bool creation, FileTimeParts& out);

// 2=空入力 / 3=無し / 7=設定失敗（作成日時の設定は Windows/macOS のみ。Linux は 7）
int FileTimeSet(const std::string& file_utf8, bool creation, const FileTimeParts& t);

// version: "%d.%d.%d.%d" 形式（Windows の GetFileVersionInfo）。
// 2=空入力 / 3=無し / 6=取得失敗（Windows 以外は常に 6）
int FileVersion(const std::string& file_utf8, std::string& version_out);

// ---- Folder 関数（戻り値は Moo エラー番号。0 = 成功） ----

int FolderExists(const std::string& folder_utf8, bool& exists_out);       // 2=空入力
int FolderCopy(const std::string& src_utf8, const std::string& dest_utf8); // 2/3/4/5/6
int FolderCreate(const std::string& folder_utf8);                          // 2=空 / 3=既存 / 4=失敗
int FolderDelete(const std::string& folder_utf8);                          // 2=空 / 3=無し / 4=失敗
int FolderMove(const std::string& src_utf8, const std::string& dest_utf8); // 2/3/4/5/6
// Moo_FolderList: 2=空入力 / 3=該当ファイルなし / 4=フォルダ無し / 5=不明なエラー。
// フォルダ直下の「ファイル」名（パスなし）を pattern で絞り、separator で連結して返す。
int FolderList(const std::string& folder_utf8, const std::string& pattern_utf8,
               const std::string& separator_utf8, std::string& list_out);

// Windows FindFirstFile 風ワイルドカード照合（テスト用に公開）。
// "*.*" は「すべて」、* = 任意列、? = 任意 1 文字（コードポイント単位）。
// ASCII は大文字小文字を無視する。
bool WildcardMatch(const std::string& name_utf8, const std::string& pattern_utf8);

// FileRead の復号処理（テスト用に公開）: UTF-8 BOM 除去 → UTF-8 妥当性検査 →
// 不正なら Windows では CP932 として復号 → 改行 CR 正規化。
std::string DecodeFileText(const std::string& bytes);

} // namespace zoo

#endif // ZOO_FILE_OPS_H

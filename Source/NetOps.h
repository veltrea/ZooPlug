// NetOps.h
//
// MooPlug の Download 2 関数 + FTP 3 関数の純粋ロジック。
//   Moo_DownloadText / Moo_DownloadFile / Moo_FTPDownload / Moo_FTPUpload / Moo_FTPDelete
// FileMaker SDK (FMWrapper) に一切依存しないので、FileMaker を起動せずに
// 単体でビルド・実行・テストできる（tests/test_netops.cpp 参照）。
//
// 実通信はプラットフォームで分岐する（NetOps.cpp）:
//   Windows : WinINet（InternetOpen / HttpOpenRequest / InternetConnect(FTP) …）
//             wininet.lib は OS 同梱なので外部依存が増えない。
//   POSIX   : libcurl（macOS はシステム libcurl、Linux も同様）。
// URL/入力の検証・パス処理・テンポラリ配置などの非通信ロジックは共通。
//
// 各関数は MooPlug のエラー番号（Moo_関数名|Err_N の N）を int で返す。0 = 成功。
// 引数不足（Err_1）は呼び出し側グルー（ZooPlug.cpp）で弾くので、ここでは扱わない。
// エラー番号の意味は Source/MooError.cpp を参照。
//
// パスはすべて UTF-8 で受け取る（Windows のダメ文字対策は PathFromUTF8 を使う）。
// bProgress（進捗ダイアログ）は Tier C 待ち。今は受け取っても無視する（グルー側で吸収）。
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_NET_OPS_H
#define ZOO_NET_OPS_H

#include <string>

namespace zoo {

// ---- HTTP(S) Download ----

// URL が http:// または https:// で始まる妥当な形か（Download 系の Err_2 判定に使用）。
// テスト用に公開する。
bool IsValidHttpUrl(const std::string& url_utf8);

// Moo_DownloadText( sFile ) — URL の本文を取得して text_out に返す。
//   2 = 入力 URL 不正 / 3 = ダウンロード失敗
// Err_4/Err_5 は本家では特定条件下で観測される戻り値だが、ZooPlug は
// メモリに直接受けるため通常 4/5 は発生しない（戻り値・主要エラーは互換）。TODO-compat。
int DownloadText(const std::string& url_utf8, std::string& text_out);

// Moo_DownloadFile( sFile ; sLocal ) — URL をローカルへ保存し、保存先フルパスを path_out に返す。
//   local_utf8 が空 → テンポラリフォルダに URL のファイル名で保存。
//   local_utf8 がフォルダ → そのフォルダ内に URL のファイル名で保存。
//   local_utf8 がファイルパス → そのパスに保存。
//   2 = 入力 URL 不正 / 3 = ダウンロード失敗 / 5 = ローカル保存先フォルダが存在しない
int DownloadFile(const std::string& url_utf8, const std::string& local_utf8,
                 std::string& path_out);

// ---- FTP ----

struct FtpParams {
    std::string server;    // ホスト名（"ftp://" 接頭辞・":port" 付きも許容。内部で正規化）
    std::string user;
    std::string password;
};

// server/user/password の空チェック（FTP 系の Err_3/4/5 判定に使用）。テスト用に公開。
//   0 = OK / 3 = server 空 / 4 = user 空 / 5 = password 空
int ValidateFtpParams(const FtpParams& ftp);

// Moo_FTPDownload( sServer; sUser; sPassword; sRemotePath; sLocalFile ) —
//   リモート remote_path を local_file へダウンロードし、保存先フルパスを path_out に返す。
//   local_file が空 → テンポラリフォルダに remote の basename で保存。
//   3/4/5 = server/user/password 不正 / 6 = インターネット接続失敗 / 7 = FTP 接続失敗 /
//   8 = ローカルに同名既存 / 10 = リモートにファイル無し / 11 = ダウンロード失敗 /
//   13 = ローカルファイルを開けない
int FTPDownload(const FtpParams& ftp, const std::string& remote_path_utf8,
                const std::string& local_file_utf8, std::string& path_out);

// Moo_FTPUpload( sServer; sUser; sPassword; sLocalFile; sRemotePath; bOverwrite ) —
//   ローカル local_file をリモート remote_path へアップロードする。成功時は何も返さない（グルーが 1）。
//   3/4/5 = server/user/password 不正 / 6 = インターネット接続失敗 / 7 = FTP 接続失敗 /
//   8 = ローカルソース無し / 11 = リモートに同名既存(上書き不可) / 12 = ローカルを開けない /
//   16 = 不明なアップロード失敗
int FTPUpload(const FtpParams& ftp, const std::string& local_file_utf8,
              const std::string& remote_path_utf8, bool overwrite);

// Moo_FTPDelete( sServer; sUser; sPass; sRemotePath ) — 未公開関数。
//   リモート remote_path を削除する。成功時は何も返さない（グルーが 1）。
//   3/4/5 = server/user/password 不正 / 6 = インターネット接続失敗 / 7 = FTP 接続失敗 /
//   8 = 削除失敗（リモート無し等）
//   ※ErrorDetail では Err_1〜8 の説明が引けない（戻り値だけ観測される）。
int FTPDelete(const FtpParams& ftp, const std::string& remote_path_utf8);

} // namespace zoo

#endif // ZOO_NET_OPS_H

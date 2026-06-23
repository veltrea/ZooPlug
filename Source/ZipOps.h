// ZipOps.h
//
// MooPlug の Zip 3 関数（Moo_ZipCompress / Moo_ZipExtract / Moo_ZipList）の純粋ロジック。
// Zip の読み書きは同梱の miniz 3.0.2（Libraries/miniz/、MIT ライセンス）を使う。
// miniz は MSVC ではファイル名を UTF-8 → _wfopen で開くので日本語パスも安全。
// FileMaker SDK (FMWrapper) に一切依存しない（tests/test_zipops.cpp 参照）。
//
// 各関数は MooPlug のエラー番号を int で返す（0 = 成功）。
// 本家仕様の注意点:
//   - ZipCompress の bTemp は Boolean/String の二刀流（true=テンポラリフォルダ /
//     false=入力と同じフォルダ / 文字列=出力パスまたはファイル名）→ glue 側で解釈し
//     ZipCompressOptions に落とす
//   - ZipExtract は本家どおり「最初の 1 ファイルのみ」展開する
//   - パスワード付き Zip（classic Zip 2.0 暗号）は未対応: 圧縮で sPassword 指定 → Err_5、
//     展開で暗号化エントリ → Err_12（"Password Required to Extract File."）
//
// Part of ZooPlug. License: see License.txt

#ifndef ZOO_ZIP_OPS_H
#define ZOO_ZIP_OPS_H

#include <string>

namespace zoo {

struct ZipCompressOptions {
    enum class Output {
        SameFolder, // 入力と同じフォルダに <名前>.zip（bTemp = false / 既定）
        TempFolder, // テンポラリフォルダに <名前>.zip（bTemp = true）
        Explicit,   // explicit_path_utf8 に出力（bTemp = 文字列）
    };
    Output output = Output::SameFolder;
    std::string explicit_path_utf8;  // Output::Explicit のとき: フルパス or ファイル名のみ
    bool overwrite_in_zip = false;   // 既存 Zip 内の同名エントリを置き換える
    bool include_folder_name = true; // フォルダ圧縮時に先頭へフォルダ名を付ける
    std::string password_utf8;       // 非空なら Err_5（未対応）
};

// Moo_ZipCompress: 2=空入力 / 3=入力無し / 4=Zip 内に同名既存(上書き不可) /
//                  5=作成失敗(パスワード指定含む) / 6=出力フォルダ無し
// 既存の Zip を指すと「追加」になる（本家仕様 "creates/adds to a Zip file"）。
// 成功時 zip_path_out に作成/更新した Zip のパス。
int ZipCompress(const std::string& path_utf8, const ZipCompressOptions& opts,
                std::string& zip_path_out);

// Moo_ZipExtract: 2=空入力 / 3=Zip 無し / 4=オープン失敗 / 5=空 Zip /
//                 6=展開失敗 / 8=展開先に同名既存(上書き不可) / 12=パスワード付き
// 最初のファイルエントリ 1 つだけを（パスを除いた名前で）展開する。
// to_temp=false なら Zip と同じフォルダへ。成功時 extracted_path_out に展開先パス。
int ZipExtract(const std::string& zip_utf8, bool to_temp, bool overwrite,
               std::string& extracted_path_out);

// Moo_ZipList: 2=空入力 / 3=Zip 無し / 4=空 Zip / 5=列挙失敗。
// ファイルエントリ（格納順）を pattern（名前部分に適用）で絞り separator で連結。
int ZipList(const std::string& zip_utf8, const std::string& pattern_utf8,
            const std::string& separator_utf8, std::string& list_out);

} // namespace zoo

#endif // ZOO_ZIP_OPS_H

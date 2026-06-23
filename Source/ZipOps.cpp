// ZipOps.cpp — Moo_ZipCompress / Moo_ZipExtract / Moo_ZipList の純粋ロジック実装
// Part of ZooPlug. License: see License.txt
//
// Zip 入出力は同梱 miniz（Libraries/miniz/）。ファイル名はすべて UTF-8 で渡す
// （miniz は MSVC では UTF-8 → _wfopen 変換を内蔵しているので日本語パスも通る）。
//
// 既存 Zip への「追加」（本家仕様 "creates/adds to a Zip file"）は、miniz が
// エントリの置き換えを直接サポートしないため、リビルド方式で実装する:
// 既存エントリを新しいテンポラリ Zip にコピー（置き換え対象はスキップ）→
// 新規エントリを追加 → 元ファイルと差し替える。

#include "ZipOps.h"

#include "FileOps.h" // PathFromUTF8 / PathToUTF8 / WildcardMatch

#include "miniz.h"

#include <filesystem>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace zoo {

namespace {

// 末尾のパス区切りを取り除く（"C:\dir\folder\" の filename() が空になるのを防ぐ）
fs::path StripTrailingSeparators(const std::string& utf8) {
    std::string s = utf8;
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) {
        s.pop_back();
    }
    return PathFromUTF8(s);
}

// Zip 内エントリ名のうち名前部分（最後の '/' 以降）
std::string EntryBaseName(const std::string& entry) {
    const std::size_t pos = entry.find_last_of("/\\");
    return pos == std::string::npos ? entry : entry.substr(pos + 1);
}

bool EqualsCaseInsensitiveAscii(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

// 圧縮対象 1 件: ディスク上のパスと Zip 内エントリ名
struct AddItem {
    fs::path source;
    std::string entry_name;
};

// 入力（ファイル or フォルダ）から圧縮対象一覧を作る。フォルダは再帰。
int CollectItems(const fs::path& src, bool include_folder_name, std::vector<AddItem>& items) {
    std::error_code ec;
    if (fs::is_directory(src, ec)) {
        const std::string prefix =
            include_folder_name ? PathToUTF8(src.filename()) + "/" : std::string();
        std::error_code iec;
        fs::recursive_directory_iterator it(src, iec);
        if (iec) return 5;
        for (const fs::recursive_directory_iterator end; it != end; it.increment(iec)) {
            if (iec) return 5;
            std::error_code fec;
            if (!it->is_regular_file(fec)) continue;
            std::error_code relc;
            const fs::path rel = fs::relative(it->path(), src, relc);
            if (relc) return 5;
            items.push_back({ it->path(), prefix + rel.generic_u8string() });
        }
    } else {
        items.push_back({ src, PathToUTF8(src.filename()) });
    }
    return 0;
}

} // namespace

int ZipCompress(const std::string& path_utf8, const ZipCompressOptions& opts,
                std::string& zip_path_out) {
    zip_path_out.clear();
    if (path_utf8.empty()) return 2;
    std::error_code ec;
    const fs::path src = StripTrailingSeparators(path_utf8);
    if (!fs::exists(src, ec)) return 3;
    if (!opts.password_utf8.empty()) return 5; // classic Zip 2.0 暗号は未対応

    // 出力 Zip パスを決める
    fs::path zip_path;
    switch (opts.output) {
        case ZipCompressOptions::Output::TempFolder: {
            std::error_code tec;
            const fs::path tmp = fs::temp_directory_path(tec);
            if (tec) return 6;
            zip_path = tmp / PathFromUTF8(PathToUTF8(src.stem()) + ".zip");
            break;
        }
        case ZipCompressOptions::Output::Explicit: {
            const std::string& given = opts.explicit_path_utf8;
            if (given.find('/') != std::string::npos || given.find('\\') != std::string::npos) {
                zip_path = PathFromUTF8(given); // フルパス指定
                std::error_code pec;
                if (!fs::is_directory(zip_path.parent_path(), pec)) return 6;
            } else {
                zip_path = src.parent_path() / PathFromUTF8(given); // ファイル名のみ指定
            }
            break;
        }
        case ZipCompressOptions::Output::SameFolder:
        default:
            zip_path = src.parent_path() / PathFromUTF8(PathToUTF8(src.stem()) + ".zip");
            break;
    }
    const std::string zip_utf8 = PathToUTF8(zip_path);

    std::vector<AddItem> items;
    const int collect_err = CollectItems(src, opts.include_folder_name, items);
    if (collect_err) return collect_err;
    if (items.empty()) return 5; // 空フォルダ（圧縮するものが無い）

    const bool zip_exists = fs::exists(zip_path, ec);

    if (!zip_exists) {
        // 新規作成
        mz_zip_archive writer;
        mz_zip_zero_struct(&writer);
        if (!mz_zip_writer_init_file(&writer, zip_utf8.c_str(), 0)) return 5;
        bool ok = true;
        for (const AddItem& item : items) {
            if (!ok) break;
            ok = mz_zip_writer_add_file(&writer, item.entry_name.c_str(),
                                        PathToUTF8(item.source).c_str(),
                                        nullptr, 0, MZ_DEFAULT_COMPRESSION) != MZ_FALSE;
        }
        if (ok) ok = mz_zip_writer_finalize_archive(&writer) != MZ_FALSE;
        mz_zip_writer_end(&writer);
        if (!ok) {
            std::error_code rec;
            fs::remove(zip_path, rec); // 不完全な Zip を残さない
            return 5;
        }
    } else {
        // 既存 Zip への追加（リビルド方式）
        mz_zip_archive reader;
        mz_zip_zero_struct(&reader);
        if (!mz_zip_reader_init_file(&reader, zip_utf8.c_str(), 0)) return 5;

        // 衝突チェック（bOverwrite 無しで同名エントリがあれば Err_4）
        bool collision = false;
        for (const AddItem& item : items) {
            if (mz_zip_reader_locate_file(&reader, item.entry_name.c_str(), nullptr, 0) >= 0) {
                collision = true;
                break;
            }
        }
        if (collision && !opts.overwrite_in_zip) {
            mz_zip_reader_end(&reader);
            return 4;
        }

        const fs::path tmp_path = PathFromUTF8(zip_utf8 + ".zootmp");
        mz_zip_archive writer;
        mz_zip_zero_struct(&writer);
        if (!mz_zip_writer_init_file(&writer, PathToUTF8(tmp_path).c_str(), 0)) {
            mz_zip_reader_end(&reader);
            return 5;
        }

        bool ok = true;
        const mz_uint count = mz_zip_reader_get_num_files(&reader);
        for (mz_uint i = 0; ok && i < count; ++i) {
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(&reader, i, &stat)) { ok = false; break; }
            bool replaced = false;
            for (const AddItem& item : items) {
                if (EqualsCaseInsensitiveAscii(item.entry_name, stat.m_filename)) {
                    replaced = true; // 置き換え対象はコピーしない
                    break;
                }
            }
            if (replaced) continue;
            ok = mz_zip_writer_add_from_zip_reader(&writer, &reader, i) != MZ_FALSE;
        }
        for (const AddItem& item : items) {
            if (!ok) break;
            ok = mz_zip_writer_add_file(&writer, item.entry_name.c_str(),
                                        PathToUTF8(item.source).c_str(),
                                        nullptr, 0, MZ_DEFAULT_COMPRESSION) != MZ_FALSE;
        }
        if (ok) ok = mz_zip_writer_finalize_archive(&writer) != MZ_FALSE;
        mz_zip_writer_end(&writer);
        mz_zip_reader_end(&reader);
        if (!ok) {
            std::error_code rec;
            fs::remove(tmp_path, rec);
            return 5;
        }
        // 元の Zip と差し替える（Windows の rename は上書きできないので先に消す）
        std::error_code rmc;
        if (!fs::remove(zip_path, rmc)) {
            fs::remove(tmp_path, rmc);
            return 5;
        }
        std::error_code mvc;
        fs::rename(tmp_path, zip_path, mvc);
        if (mvc) return 5;
    }

    zip_path_out = zip_utf8;
    return 0;
}

int ZipExtract(const std::string& zip_utf8, bool to_temp, bool overwrite,
               std::string& extracted_path_out) {
    extracted_path_out.clear();
    if (zip_utf8.empty()) return 2;
    std::error_code ec;
    const fs::path zip_path = PathFromUTF8(zip_utf8);
    if (!fs::exists(zip_path, ec)) return 3;

    mz_zip_archive reader;
    mz_zip_zero_struct(&reader);
    if (!mz_zip_reader_init_file(&reader, zip_utf8.c_str(), 0)) return 4;

    const mz_uint count = mz_zip_reader_get_num_files(&reader);
    if (count == 0) { mz_zip_reader_end(&reader); return 5; }

    // 最初の「ファイル」エントリを探す（本家仕様: 単一ファイルのみ展開）
    int target = -1;
    mz_zip_archive_file_stat stat;
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&reader, i)) continue;
        if (!mz_zip_reader_file_stat(&reader, i, &stat)) { mz_zip_reader_end(&reader); return 7; }
        target = static_cast<int>(i);
        break;
    }
    if (target < 0) { mz_zip_reader_end(&reader); return 5; }

    if (mz_zip_reader_is_file_encrypted(&reader, static_cast<mz_uint>(target))) {
        mz_zip_reader_end(&reader);
        return 12; // Password Required to Extract File.
    }

    fs::path dest_dir;
    if (to_temp) {
        std::error_code tec;
        dest_dir = fs::temp_directory_path(tec);
        if (tec) { mz_zip_reader_end(&reader); return 6; }
    } else {
        dest_dir = zip_path.parent_path();
    }
    // Zip 内のサブパスは捨てて名前だけで展開する（zip-slip 対策にもなる）
    const fs::path dest = dest_dir / PathFromUTF8(EntryBaseName(stat.m_filename));

    std::error_code dec;
    if (fs::exists(dest, dec) && !overwrite) { mz_zip_reader_end(&reader); return 8; }

    const mz_bool ok = mz_zip_reader_extract_to_file(&reader, static_cast<mz_uint>(target),
                                                     PathToUTF8(dest).c_str(), 0);
    mz_zip_reader_end(&reader);
    if (!ok) return 6;

    extracted_path_out = PathToUTF8(dest);
    return 0;
}

int ZipList(const std::string& zip_utf8, const std::string& pattern_utf8,
            const std::string& separator_utf8, std::string& list_out) {
    list_out.clear();
    if (zip_utf8.empty()) return 2;
    std::error_code ec;
    if (!fs::exists(PathFromUTF8(zip_utf8), ec)) return 3;

    mz_zip_archive reader;
    mz_zip_zero_struct(&reader);
    if (!mz_zip_reader_init_file(&reader, zip_utf8.c_str(), 0)) return 5;

    const mz_uint count = mz_zip_reader_get_num_files(&reader);
    if (count == 0) { mz_zip_reader_end(&reader); return 4; }

    std::vector<std::string> names;
    for (mz_uint i = 0; i < count; ++i) {
        if (mz_zip_reader_is_file_a_directory(&reader, i)) continue;
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&reader, i, &stat)) { mz_zip_reader_end(&reader); return 5; }
        const std::string entry = stat.m_filename;
        // パターンは名前部分に当てる。出力は格納名そのまま・格納順
        if (WildcardMatch(EntryBaseName(entry), pattern_utf8)) {
            names.push_back(entry);
        }
    }
    mz_zip_reader_end(&reader);

    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) list_out += separator_utf8;
        list_out += names[i];
    }
    return 0;
}

} // namespace zoo

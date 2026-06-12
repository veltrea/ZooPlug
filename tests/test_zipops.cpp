// test_zipops.cpp
//
// ZipOps（Moo_ZipCompress / Moo_ZipExtract / Moo_ZipList の純粋ロジック）を
// FileMaker 無しで検証する。一時フォルダの中だけで完結する。
//
// Part of ZooPlug. License: see License.txt

#include "ZipOps.h"
#include "FileOps.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    if (ok) {
        std::printf("  [PASS] %s\n", label);
    } else {
        std::printf("  [FAIL] %s\n", label);
        ++g_failures;
    }
}

void check_eq(const std::string& actual, const std::string& expected, const char* label) {
    if (actual == expected) {
        std::printf("  [PASS] %s\n", label);
    } else {
        std::printf("  [FAIL] %s\n         expected=[%s]\n         actual  =[%s]\n",
                    label, expected.c_str(), actual.c_str());
        ++g_failures;
    }
}

void check_int(int actual, int expected, const char* label) {
    if (actual == expected) {
        std::printf("  [PASS] %s\n", label);
    } else {
        std::printf("  [FAIL] %s (expected=%d actual=%d)\n", label, expected, actual);
        ++g_failures;
    }
}

// パスの等価比較（Windows の \ と / の表記差を吸収する）
void check_path_eq(const std::string& actual, const std::string& expected, const char* label) {
    const bool same = zoo::PathFromUTF8(actual).lexically_normal() ==
                      zoo::PathFromUTF8(expected).lexically_normal();
    if (same) {
        std::printf("  [PASS] %s\n", label);
    } else {
        std::printf("  [FAIL] %s\n         expected=[%s]\n         actual  =[%s]\n",
                    label, expected.c_str(), actual.c_str());
        ++g_failures;
    }
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
    using namespace zoo;

    const fs::path root = fs::temp_directory_path() / "zooplug_test_zipops";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const std::string base = PathToUTF8(root);

    // テスト用のフォルダ構成を作る
    const std::string data = base + "/data";
    FolderCreate(data + "/sub");
    FileWrite(data + "/a.txt", "hello zip", false);
    FileWrite(data + "/b.txt", "second file", false);
    FileWrite(data + "/d.csv", "1,2,3", false);
    FileWrite(data + "/sub/c.txt", "nested", false);
    FileWrite(data + u8"/表予能.txt", "dame-moji", false);

    ZipCompressOptions same_folder; // 既定: SameFolder / 上書きなし / フォルダ名あり

    std::printf("ZipCompress (single file, same folder):\n");
    std::string zip_path;
    check_int(ZipCompress(data + "/a.txt", same_folder, zip_path), 0, "compress a.txt");
    check_path_eq(zip_path, data + "/a.zip", "zip created next to input");
    check(fs::exists(PathFromUTF8(zip_path)), "zip file exists");
    check_int(ZipCompress(base + "/nope.txt", same_folder, zip_path), 3, "missing input -> Err_3");
    check_int(ZipCompress("", same_folder, zip_path), 2, "empty input -> Err_2");
    {
        ZipCompressOptions with_password = same_folder;
        with_password.password_utf8 = "secret";
        check_int(ZipCompress(data + "/a.txt", with_password, zip_path), 5,
                  "password requested -> Err_5 (not supported)");
    }

    std::printf("ZipList:\n");
    {
        std::string list;
        check_int(ZipList(data + "/a.zip", "*.*", "|", list), 0, "list single-file zip");
        check_eq(list, "a.txt", "entry name stored without path");
        check_int(ZipList(base + "/nope.zip", "*.*", "|", list), 3, "missing zip -> Err_3");
        check_int(ZipList("", "*.*", "|", list), 2, "empty input -> Err_2");
    }

    std::printf("ZipExtract:\n");
    {
        std::string extracted;
        // a.zip の隣には元の a.txt がある → 上書きなしは Err_8
        check_int(ZipExtract(data + "/a.zip", false, false, extracted), 8,
                  "destination exists without overwrite -> Err_8");
        check_int(ZipExtract(data + "/a.zip", false, true, extracted), 0, "extract with overwrite");
        check_path_eq(extracted, data + "/a.txt", "extracted next to zip");
        std::string text;
        FileRead(extracted, text);
        check_eq(text, "hello zip", "content round-trips");
        check_int(ZipExtract(base + "/nope.zip", false, false, extracted), 3, "missing zip -> Err_3");
    }

    std::printf("ZipCompress (folder, recursive):\n");
    {
        std::string folder_zip;
        check_int(ZipCompress(data, same_folder, folder_zip), 0, "compress folder");
        check_path_eq(folder_zip, base + "/data.zip", "folder zip placed in parent");
        std::string list;
        check_int(ZipList(folder_zip, "*.*", "|", list), 0, "list folder zip");
        check(Contains(list, "data/a.txt"), "folder name prefixed (bFolderName default)");
        check(Contains(list, "data/sub/c.txt"), "nested file included");
        check(Contains(list, u8"data/表予能.txt"), "Japanese entry name survives");
        check_int(ZipList(folder_zip, "*.csv", "|", list), 0, "pattern filter");
        check_eq(list, "data/d.csv", "only csv listed");

        ZipCompressOptions no_prefix = same_folder;
        no_prefix.include_folder_name = false;
        no_prefix.output = ZipCompressOptions::Output::Explicit;
        no_prefix.explicit_path_utf8 = "flat.zip";
        std::string flat_zip;
        check_int(ZipCompress(data, no_prefix, flat_zip), 0, "compress folder without prefix");
        check_path_eq(flat_zip, base + "/flat.zip", "explicit filename lands next to input");
        check_int(ZipList(flat_zip, "a.txt", "|", list), 0, "list flat zip");
        check_eq(list, "a.txt", "no folder prefix when bFolderName = false");
    }

    std::printf("ZipCompress (add to existing zip):\n");
    {
        ZipCompressOptions to_existing;
        to_existing.output = ZipCompressOptions::Output::Explicit;
        to_existing.explicit_path_utf8 = "combo.zip";
        std::string combo;
        check_int(ZipCompress(data + "/a.txt", to_existing, combo), 0, "create combo.zip with a.txt");
        check_int(ZipCompress(data + "/b.txt", to_existing, combo), 0, "add b.txt to existing zip");
        std::string list;
        ZipList(combo, "*.*", "|", list);
        check_eq(list, "a.txt|b.txt", "both entries present after add");
        check_int(ZipCompress(data + "/a.txt", to_existing, combo), 4,
                  "same entry again without overwrite -> Err_4");
        ZipCompressOptions overwrite_entry = to_existing;
        overwrite_entry.overwrite_in_zip = true;
        check_int(ZipCompress(data + "/a.txt", overwrite_entry, combo), 0,
                  "same entry with overwrite -> rebuilt");
        ZipList(combo, "*.*", "|", list);
        check_eq(list, "b.txt|a.txt", "entry replaced, not duplicated");
    }

    std::printf("ZipCompress (explicit full path):\n");
    {
        ZipCompressOptions full_path;
        full_path.output = ZipCompressOptions::Output::Explicit;
        full_path.explicit_path_utf8 = base + "/out/explicit.zip";
        std::string out;
        check_int(ZipCompress(data + "/a.txt", full_path, out), 6,
                  "output folder missing -> Err_6");
        FolderCreate(base + "/out");
        check_int(ZipCompress(data + "/a.txt", full_path, out), 0, "full path output ok");
        check_path_eq(out, base + "/out/explicit.zip", "explicit path respected");
    }

    std::printf("empty zip:\n");
    {
        // 空 Zip = EOCD レコードだけの 22 バイト
        const std::string empty_zip = base + "/empty.zip";
        std::ofstream out(PathFromUTF8(empty_zip), std::ios::binary);
        const unsigned char eocd[22] = { 0x50, 0x4B, 0x05, 0x06 };
        out.write(reinterpret_cast<const char*>(eocd), sizeof eocd);
        out.close();
        std::string list, extracted;
        check_int(ZipList(empty_zip, "*.*", "|", list), 4, "empty zip list -> Err_4");
        check_int(ZipExtract(empty_zip, false, false, extracted), 5, "empty zip extract -> Err_5");
    }

    std::printf("Japanese file round-trip through zip:\n");
    {
        ZipCompressOptions jp_zip;
        jp_zip.output = ZipCompressOptions::Output::Explicit;
        jp_zip.explicit_path_utf8 = u8"日本語書庫.zip";
        std::string out;
        check_int(ZipCompress(data + u8"/表予能.txt", jp_zip, out), 0, "compress dame-moji file");
        std::string extracted;
        check_int(ZipExtract(out, true, true, extracted), 0, "extract to temp");
        std::string text;
        FileRead(extracted, text);
        check_eq(text, "dame-moji", "content survives");
        FileDelete(extracted);
    }

    fs::remove_all(root, ec);

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

// test_fileops.cpp
//
// FileOps（File/Folder 純粋ロジック）を FileMaker 無しで検証するスタンドアロンテスト。
// 一時フォルダ（fs::temp_directory_path()/zooplug_test_fileops）の中だけで完結する。
// 日本語ファイル名は CP932 の「ダメ文字」（2 バイト目が 0x5C の漢字: ソ・表・予・能）を
// 含めて検証する。
//
// Part of ZooPlug. License: see License.txt

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

// ファイルの生バイトを読む（改行変換の検証用）
std::string RawBytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    using namespace zoo;

    const fs::path root = fs::temp_directory_path() / "zooplug_test_fileops";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const std::string base = PathToUTF8(root);

    std::printf("WildcardMatch:\n");
    check(WildcardMatch("noext", "*.*"), "*.* matches extensionless (FindFirstFile style)");
    check(WildcardMatch("a.txt", "*.txt"), "*.txt matches a.txt");
    check(WildcardMatch("A.TXT", "*.txt"), "case-insensitive ASCII");
    check(!WildcardMatch("a.txt.bak", "*.txt"), "*.txt does not match .bak");
    check(WildcardMatch("data1.csv", "data?.csv"), "? matches one char");
    check(!WildcardMatch("data12.csv", "data?.csv"), "? does not match two chars");
    check(WildcardMatch(u8"表予能.txt", "*.txt"), "Japanese (dame-moji) name with *.txt");
    check(WildcardMatch(u8"ソa.txt", u8"?a.txt"), "? consumes one multibyte codepoint");
    check(WildcardMatch("abc", "*"), "* matches everything");
    check(!WildcardMatch("abc", "a?"), "pattern shorter than name fails");

    std::printf("HumanReadableSize:\n");
    check_eq(HumanReadableSize(0), "0 bytes", "0 bytes");
    check_eq(HumanReadableSize(1023), "1023 bytes", "1023 bytes");
    check_eq(HumanReadableSize(1024), "1.00 KB", "1.00 KB");
    check_eq(HumanReadableSize(1536), "1.50 KB", "1.50 KB");
    check_eq(HumanReadableSize(10 * 1024), "10.0 KB", "10.0 KB");
    check_eq(HumanReadableSize(102400), "100 KB", "100 KB");
    check_eq(HumanReadableSize(2147483647ULL), "1.99 GB", "1.99 GB (StrFormatByteSize truncation)");

    std::printf("DecodeFileText:\n");
    check_eq(DecodeFileText("\xEF\xBB\xBFhello"), "hello", "UTF-8 BOM stripped");
    check_eq(DecodeFileText("a\r\nb\nc"), "a\rb\rc", "newlines normalized to CR");
    check_eq(DecodeFileText(u8"日本語"), u8"日本語", "valid UTF-8 passes through");
#if defined(_WIN32)
    // CP932 の「表」(0x95 0x5C) が正しく UTF-8 になるか
    check_eq(DecodeFileText("\x95\x5C"), u8"表", "CP932 fallback decodes dame-moji");
#endif

    std::printf("FileWrite / FileRead:\n");
    const std::string jp_file = base + u8"/ソ表予能テスト.txt";
    check_int(FileWrite(jp_file, u8"一行目\r二行目", false), 0, "write Japanese file (dame-moji name)");
    {
        std::string text;
        check_int(FileRead(jp_file, text), 0, "read it back");
        check_eq(text, u8"一行目\r二行目", "CR newline round-trips");
    }
    {
        // FileWrite は OS ネイティブ改行で書く
        const std::string raw = RawBytes(PathFromUTF8(jp_file));
#if defined(_WIN32)
        check(raw.find("\r\n") != std::string::npos, "native newline on disk (CRLF)");
#else
        check(raw.find('\r') == std::string::npos && raw.find('\n') != std::string::npos,
              "native newline on disk (LF)");
#endif
    }
    check_int(FileWrite(jp_file, "x", false), 3, "write to existing without append -> Err_3");
    check_int(FileWrite(jp_file, u8"\r追記", true), 0, "append works");
    {
        std::string text;
        FileRead(jp_file, text);
        check_eq(text, u8"一行目\r二行目\r追記", "appended content read back");
    }
    check_int(FileWrite("", "x", false), 2, "empty path -> Err_2");
    {
        std::string text;
        check_int(FileRead(base + "/nonexistent.txt", text), 3, "read nonexistent -> Err_3");
    }

    std::printf("FileExists:\n");
    {
        bool exists = false;
        check_int(FileExists(jp_file, exists), 0, "exists check ok");
        check(exists, "existing file -> true");
        FileExists(base + "/nope.txt", exists);
        check(!exists, "missing file -> false");
        check_int(FileExists("", exists), 2, "empty path -> Err_2");
    }

    std::printf("FileCopy:\n");
    const std::string copy_dest = base + u8"/コピー先.txt";
    check_int(FileCopy(jp_file, copy_dest, false), 0, "copy ok");
    check_int(FileCopy(jp_file, copy_dest, false), 5, "dest exists without overwrite -> Err_5");
    check_int(FileCopy(jp_file, copy_dest, true), 0, "overwrite ok");
    check_int(FileCopy(base + "/nope.txt", copy_dest, true), 3, "missing source -> Err_3");
    check_int(FileCopy("", copy_dest, false), 2, "empty source -> Err_2");
    check_int(FileCopy(jp_file, "", false), 4, "empty dest -> Err_4");

    std::printf("FileMove:\n");
    const std::string move_dest = base + u8"/移動先.txt";
    check_int(FileMove(copy_dest, move_dest, false), 0, "move ok");
    check(!fs::exists(PathFromUTF8(copy_dest)), "source gone after move");
    check(fs::exists(PathFromUTF8(move_dest)), "dest present after move");
    check_int(FileMove(jp_file, move_dest, false), 5, "dest exists without overwrite -> Err_5");
    check_int(FileMove(jp_file, move_dest, true), 0, "move with overwrite ok");
    check_int(FileMove(base + "/nope.txt", base + "/x.txt", false), 3, "missing source -> Err_3");

    std::printf("FileDelete:\n");
    check_int(FileDelete(move_dest), 0, "delete ok");
    check_int(FileDelete(move_dest), 4, "already gone -> Err_4");
    check_int(FileDelete(""), 2, "empty path -> Err_2");

    std::printf("FileSize / FileTime:\n");
    const std::string size_file = base + "/size.bin";
    {
        std::ofstream out(PathFromUTF8(size_file), std::ios::binary);
        const std::string payload(1536, 'x');
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
    {
        std::uint64_t bytes = 0;
        check_int(FileSize(size_file, bytes), 0, "size ok");
        check(bytes == 1536, "size is 1536");
        check_int(FileSize(base + "/nope.bin", bytes), 3, "missing file -> Err_3");
    }
    {
        FileTimeParts set_to;
        set_to.year = 2020; set_to.month = 5; set_to.day = 6;
        set_to.hour = 7; set_to.minute = 8; set_to.second = 9;
        check_int(FileTimeSet(size_file, false, set_to), 0, "set modified time");
        FileTimeParts got;
        check_int(FileTimeGet(size_file, false, got), 0, "get modified time");
        check(got.year == 2020 && got.month == 5 && got.day == 6 &&
              got.hour == 7 && got.minute == 8 && got.second == 9,
              "modified time round-trips (local)");
        check_int(FileTimeGet(base + "/nope.bin", false, got), 3, "missing file -> Err_3");
#if defined(_WIN32) || defined(__APPLE__)
        FileTimeParts created;
        check_int(FileTimeGet(size_file, true, created), 0, "get creation time");
        check(created.year >= 2020, "creation year sane");
#endif
    }

    std::printf("FileVersion:\n");
    {
        std::string version;
#if defined(_WIN32)
        check_int(FileVersion("C:\\Windows\\System32\\kernel32.dll", version), 0, "kernel32 version ok");
        check(!version.empty() && version.find('.') != std::string::npos, "version has dots");
#else
        check_int(FileVersion(size_file, version), 6, "non-Windows -> Err_6");
#endif
        check_int(FileVersion(base + "/nope.dll", version), 3, "missing file -> Err_3");
    }

    std::printf("Folder functions:\n");
    const std::string folder_a = base + u8"/フォルダソ";
    check_int(FolderCreate(folder_a + "/nested"), 0, "create nested folders");
    check_int(FolderCreate(folder_a), 3, "already exists -> Err_3");
    {
        bool exists = false;
        check_int(FolderExists(folder_a, exists), 0, "folder exists check ok");
        check(exists, "created folder -> true");
        FolderExists(base + "/nope", exists);
        check(!exists, "missing folder -> false");
        FolderExists(size_file, exists);
        check(!exists, "file is not a folder");
    }
    FileWrite(folder_a + "/b.txt", "b", false);
    FileWrite(folder_a + "/A.txt", "a", false);
    FileWrite(folder_a + "/c.csv", "c", false);
    FileWrite(folder_a + u8"/表予能.txt", "jp", false);

    std::printf("FolderList:\n");
    {
        std::string list;
        check_int(FolderList(folder_a, "*.*", "|", list), 0, "list all");
        check_eq(list, u8"A.txt|b.txt|c.csv|表予能.txt", "all files sorted, folders excluded");
        check_int(FolderList(folder_a, "*.txt", "; ", list), 0, "list *.txt with custom separator");
        check_eq(list, u8"A.txt; b.txt; 表予能.txt", "pattern + separator respected");
        check_int(FolderList(folder_a, "*.nope", "|", list), 3, "no match -> Err_3");
        check_int(FolderList(base + "/nope", "*.*", "|", list), 4, "missing folder -> Err_4");
        check_int(FolderList("", "*.*", "|", list), 2, "empty input -> Err_2");
    }

    std::printf("FolderCopy / FolderMove / FolderDelete:\n");
    const std::string folder_b = base + u8"/フォルダ写し";
    check_int(FolderCopy(folder_a, folder_b), 0, "copy folder recursively");
    check(fs::exists(PathFromUTF8(folder_b + "/b.txt")), "copied file present");
    check(fs::is_directory(PathFromUTF8(folder_b + "/nested")), "copied subfolder present");
    check_int(FolderCopy(folder_a, folder_b), 5, "dest exists -> Err_5");
    check_int(FolderCopy(base + "/nope", base + "/x"), 3, "missing source -> Err_3");
    check_int(FolderCopy(folder_a, ""), 4, "empty dest -> Err_4");

    const std::string folder_c = base + u8"/フォルダ移動先";
    check_int(FolderMove(folder_b, folder_c), 0, "move folder");
    check(!fs::exists(PathFromUTF8(folder_b)), "move source gone");
    check(fs::exists(PathFromUTF8(folder_c + "/b.txt")), "moved content present");
    check_int(FolderMove(folder_a, folder_c), 5, "dest exists -> Err_5");

    check_int(FolderDelete(folder_c), 0, "delete folder recursively");
    check_int(FolderDelete(folder_c), 3, "already gone -> Err_3");
    check_int(FolderDelete(""), 2, "empty input -> Err_2");

    fs::remove_all(root, ec);

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

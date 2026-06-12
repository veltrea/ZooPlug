// test_mooerror.cpp
//
// MooError（Err 文字列生成 + ErrorDetail マップ）を FileMaker 無しで検証する。
//   macOS / Linux : c++ -std=c++17 -I../Source ../Source/MooError.cpp test_mooerror.cpp -o test && ./test
//   Windows (MSVC): cl /std:c++17 /EHsc /I..\Source ..\Source\MooError.cpp test_mooerror.cpp
//
// Part of ZooPlug. License: see License.txt

#include "MooError.h"

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void check_eq(const std::string& actual, const std::string& expected, const char* label) {
    if (actual == expected) {
        std::printf("  [PASS] %s\n", label);
    } else {
        std::printf("  [FAIL] %s\n         expected=[%s]\n         actual  =[%s]\n",
                    label, expected.c_str(), actual.c_str());
        ++g_failures;
    }
}

} // namespace

int main() {
    using namespace zoo;

    std::printf("MakeMooError:\n");
    check_eq(MakeMooError("Moo_FileCopy", 3), "Moo_FileCopy|Err_3", "basic format");
    check_eq(MakeMooError("Moo_FTPUpload", 16), "Moo_FTPUpload|Err_16", "two-digit number");

    std::printf("kMooVersionString:\n");
    check_eq(kMooVersionString, "MooPlug 0.4.9", "version literal");

    std::printf("MooErrorDetail (documented entries):\n");
    check_eq(MooErrorDetail("Moo_FileCopy|Err_3"), "Source file does not exist.", "FileCopy Err_3");
    check_eq(MooErrorDetail("Moo_FileDelete|Err_4"), "Source file doesn't exist.", "FileDelete Err_4 (doesn't)");
    check_eq(MooErrorDetail("Moo_DialogColour|Err_2"), "Dialog cancelled by user.", "DialogColour Err_2");
    check_eq(MooErrorDetail("Moo_FolderList|Err_3"), "Input folder contains no files.", "FolderList Err_3");
    check_eq(MooErrorDetail("Moo_FTPUpload|Err_15"), "Error deleting existing file on FTP Server.", "FTPUpload Err_15");

    std::printf("MooErrorDetail (binary-only / binary-corrected entries):\n");
    check_eq(MooErrorDetail("Moo_DownloadFile|Err_4"), "File download cancelled by user.", "DownloadFile Err_4 (binary wording)");
    check_eq(MooErrorDetail("Moo_ZipCompress|Err_4"), "File already exists in Zip archive.", "ZipCompress Err_4 (binary wording)");
    check_eq(MooErrorDetail("Moo_ZipCompress|Err_6"), "Output folder doesn't exist.", "ZipCompress Err_6 (binary only)");
    check_eq(MooErrorDetail("Moo_FolderList|Err_4"), "Input folder doesn't exist.", "FolderList Err_4 (binary only)");
    check_eq(MooErrorDetail("Moo_FolderList|Err_5"), "Unknown error.", "FolderList Err_5 (binary only)");
    check_eq(MooErrorDetail("Moo_FileMove|Err_7"), "Error deleting destination file.", "FileMove Err_7 (binary only)");
    check_eq(MooErrorDetail("Moo_FileInfo|Err_7"), "Error setting file info.", "FileInfo Err_7 (binary only)");
    check_eq(MooErrorDetail("Moo_ZipExtract|Err_12"), "Password Required to Extract File.", "ZipExtract Err_12 (binary only)");

    std::printf("MooErrorDetail (binary verbatim quirks):\n");
    check_eq(MooErrorDetail("Moo_Hash|Err_4"), "Input file doesn't exist", "Hash Err_4 has no trailing period");
    check_eq(MooErrorDetail("Moo_Hash|Err_5"), "Error generating hash", "Hash Err_5 has no trailing period");
    check_eq(MooErrorDetail("Moo_ZipList|Err_4"), "Empty Zip file", "ZipList Err_4 has no trailing period");
    check_eq(MooErrorDetail("Moo_HotkeyList|Err_2"), "No hotkeys currentley set.", "original 'currentley' typo preserved");

    std::printf("MooErrorDetail (unknown / absent codes):\n");
    check_eq(MooErrorDetail("Moo_FileWrite|Err_6"), "", "FileWrite Err_6 absent in 0.4.9");
    check_eq(MooErrorDetail("Moo_FTPDelete|Err_1"), "", "FTPDelete not in ErrorDetail chain");
    check_eq(MooErrorDetail("Nonsense"), "", "garbage input -> empty");
    check_eq(MooErrorDetail(""), "", "empty input -> empty");

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

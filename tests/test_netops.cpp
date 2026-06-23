// test_netops.cpp
//
// NetOps の純粋ロジック（URL/FTP パラメータ検証）と、ローカルサーバがあれば
// HTTP/FTP の実往復を FileMaker 無しで検証するスタンドアロンテスト。
//
// 純粋ロジックは常に検証する。実通信テストは環境変数が設定されていれば実行し、
// なければ SKIP する（CI でネットワーク不要・verify 手順がサーバを立てて環境変数を渡す）:
//   ZOO_TEST_HTTP_URL     例) http://127.0.0.1:8765/zoo_net_probe.txt
//   ZOO_TEST_HTTP_EXPECT  期待する本文（省略時は「rc=0 かつ非空」だけ確認）
//   ZOO_TEST_FTP_SERVER / ZOO_TEST_FTP_USER / ZOO_TEST_FTP_PASS / ZOO_TEST_FTP_REMOTE
//                         FTP 往復（アップロード→ダウンロード→削除）に使う。
//
//   macOS/Linux: c++ -std=c++17 -I../Source ../Source/NetOps.cpp ../Source/FileOps.cpp \
//                  ../Source/ShellExec.cpp ../Source/ProcessRun.cpp test_netops.cpp -lcurl -o t && ./t
//
// Part of ZooPlug. License: see License.txt

#include "NetOps.h"
#include "FileOps.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {

int g_failures = 0;

void check_true(bool cond, const char* label) {
    if (cond) {
        std::printf("  [PASS] %s\n", label);
    } else {
        std::printf("  [FAIL] %s\n", label);
        ++g_failures;
    }
}

void check_eq_int(int actual, int expected, const char* label) {
    if (actual == expected) {
        std::printf("  [PASS] %s\n", label);
    } else {
        std::printf("  [FAIL] %s  expected=%d actual=%d\n", label, expected, actual);
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

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

} // namespace

int main() {
    using namespace zoo;

    // ---- 純粋ロジック: IsValidHttpUrl ----
    std::printf("IsValidHttpUrl:\n");
    check_true(IsValidHttpUrl("http://example.com/a.txt"), "http accepted");
    check_true(IsValidHttpUrl("https://example.com"), "https accepted");
    check_true(IsValidHttpUrl("  HTTPS://EXAMPLE.com/x "), "scheme case-insensitive, trimmed");
    check_true(!IsValidHttpUrl(""), "empty rejected");
    check_true(!IsValidHttpUrl("ftp://host/f"), "ftp rejected");
    check_true(!IsValidHttpUrl("example.com"), "no scheme rejected");
    check_true(!IsValidHttpUrl("http://"), "scheme with no host rejected");

    // ---- 純粋ロジック: ValidateFtpParams ----
    std::printf("ValidateFtpParams:\n");
    {
        FtpParams ok; ok.server = "ftp.example.com"; ok.user = "u"; ok.password = "p";
        check_eq_int(ValidateFtpParams(ok), 0, "all present -> 0");
        FtpParams noServer = ok; noServer.server = "";
        check_eq_int(ValidateFtpParams(noServer), 3, "empty server -> 3");
        FtpParams noUser = ok; noUser.user = "";
        check_eq_int(ValidateFtpParams(noUser), 4, "empty user -> 4");
        FtpParams noPass = ok; noPass.password = "";
        check_eq_int(ValidateFtpParams(noPass), 5, "empty password -> 5");
    }

    // ---- 入力検証パスのエラー番号（通信に到達しない）----
    std::printf("error paths (no network):\n");
    {
        std::string out;
        check_eq_int(DownloadText("not a url", out), 2, "DownloadText invalid url -> 2");
        check_eq_int(DownloadFile("ftp://x/y", "", out), 2, "DownloadFile invalid url -> 2");
        FtpParams empty;
        check_eq_int(FTPDownload(empty, "r", "", out), 3, "FTPDownload empty server -> 3");
        check_eq_int(FTPUpload(empty, "l", "r", false), 3, "FTPUpload empty server -> 3");
        check_eq_int(FTPDelete(empty, "r"), 3, "FTPDelete empty server -> 3");
    }
    // DownloadFile: ローカル保存先フォルダが存在しない -> 5
    {
        std::string out;
        const int e = DownloadFile("http://127.0.0.1:9/x.txt",
                                   "/zoo_nonexistent_dir_xyz/sub/file.txt", out);
        check_true(e == 5, "DownloadFile missing local folder -> 5 (before any fetch)");
    }

    // ---- 実 HTTP 往復（ZOO_TEST_HTTP_URL が無ければ SKIP）----
    std::printf("HTTP round-trip (live):\n");
    const std::string http_url = env("ZOO_TEST_HTTP_URL");
    if (http_url.empty()) {
        std::printf("  [SKIP] ZOO_TEST_HTTP_URL 未設定のため HTTP live テストを省略\n");
    } else {
        const std::string expect = env("ZOO_TEST_HTTP_EXPECT");
        std::string body;
        const int rc = DownloadText(http_url, body);
        check_eq_int(rc, 0, "DownloadText rc=0");
        if (!expect.empty()) check_eq(body, expect, "DownloadText body matches expected");
        else check_true(!body.empty(), "DownloadText body non-empty");

        std::string path;
        const int rc2 = DownloadFile(http_url, "", path);
        check_eq_int(rc2, 0, "DownloadFile rc=0");
        if (rc2 == 0) {
            std::ifstream ifs(PathFromUTF8(path), std::ios::binary);
            std::string disk((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
            if (!expect.empty()) check_eq(disk, expect, "DownloadFile saved content matches");
            else check_true(!disk.empty(), "DownloadFile saved content non-empty");
            std::error_code ec; std::filesystem::remove(PathFromUTF8(path), ec);
        }
    }

    // ---- 実 FTP 往復（環境変数が無ければ SKIP）: upload -> download -> delete ----
    std::printf("FTP round-trip (live):\n");
    const std::string ftp_server = env("ZOO_TEST_FTP_SERVER");
    if (ftp_server.empty()) {
        std::printf("  [SKIP] ZOO_TEST_FTP_SERVER 未設定のため FTP live テストを省略\n");
    } else {
        FtpParams ftp;
        ftp.server   = ftp_server;
        ftp.user     = env("ZOO_TEST_FTP_USER");
        ftp.password = env("ZOO_TEST_FTP_PASS");
        std::string remote = env("ZOO_TEST_FTP_REMOTE");
        if (remote.empty()) remote = "zoo_net_test.txt";

        // ローカルに送信元を用意
        const std::string content = u8"zoo ftp 表予能 \n line2\n";
        auto tmp = std::filesystem::temp_directory_path() / "zoo_ftp_src.txt";
        { std::ofstream o(tmp, std::ios::binary); o << content; }

        const int up = FTPUpload(ftp, PathToUTF8(tmp), remote, true);
        check_eq_int(up, 0, "FTPUpload rc=0");

        if (up == 0) {
            auto dl = std::filesystem::temp_directory_path() / "zoo_ftp_dl.txt";
            std::error_code ec; std::filesystem::remove(dl, ec);
            std::string got;
            const int down = FTPDownload(ftp, remote, PathToUTF8(dl), got);
            check_eq_int(down, 0, "FTPDownload rc=0");
            if (down == 0) {
                std::ifstream ifs(PathFromUTF8(got), std::ios::binary);
                std::string disk((std::istreambuf_iterator<char>(ifs)),
                                 std::istreambuf_iterator<char>());
                check_eq(disk, content, "FTP round-trip content matches");
                std::filesystem::remove(dl, ec);
            }
            const int del = FTPDelete(ftp, remote);
            check_eq_int(del, 0, "FTPDelete rc=0");
        }
        std::error_code ec; std::filesystem::remove(tmp, ec);
    }

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

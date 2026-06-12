// test_hash.cpp
//
// HashImpl（MD5/SHA-1/SHA-256/SHA-512）を NIST / RFC の公式テストベクタで検証する。
//   - "abc" と 448 ビット境界ベクタは FIPS 180-4 付録 / RFC 1321 §A.5 の値
//   - 1,000,000 × 'a' のロングベクタでストリーミング（ブロック跨ぎ）も検証
//
// Part of ZooPlug. License: see License.txt

#include "HashImpl.h"
#include "FileOps.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

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

void check_int(int actual, int expected, const char* label) {
    if (actual == expected) {
        std::printf("  [PASS] %s\n", label);
    } else {
        std::printf("  [FAIL] %s (expected=%d actual=%d)\n", label, expected, actual);
        ++g_failures;
    }
}

std::string HashStr(const char* algo, const std::string& text) {
    std::string out;
    zoo::HashString(algo, text, out);
    return out;
}

} // namespace

int main() {
    using namespace zoo;

    const std::string abc = "abc";
    const std::string two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const std::string million_a(1000000, 'a');

    std::printf("MD5 (RFC 1321):\n");
    check_eq(HashStr("md5", abc), "900150983cd24fb0d6963f7d28e17f72", "md5(abc)");
    check_eq(HashStr("md5", "message digest"), "f96b697d7cb7938d525a2f31aaf161d0", "md5(message digest)");
    check_eq(HashStr("md5", million_a), "7707d6ae4e027c70eea2a935c2296f21", "md5(1M x a)");

    std::printf("SHA-1 (FIPS 180-4):\n");
    check_eq(HashStr("sha1", abc), "a9993e364706816aba3e25717850c26c9cd0d89d", "sha1(abc)");
    check_eq(HashStr("sha1", two_blocks), "84983e441c3bd26ebaae4aa1f95129e5e54670f1", "sha1(448-bit vector)");
    check_eq(HashStr("sha1", million_a), "34aa973cd4c4daa4f61eeb2bdbad27316534016f", "sha1(1M x a)");

    std::printf("SHA-256 (FIPS 180-4):\n");
    check_eq(HashStr("sha256", abc),
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256(abc)");
    check_eq(HashStr("sha256", two_blocks),
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256(448-bit vector)");
    check_eq(HashStr("sha256", million_a),
             "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256(1M x a)");

    std::printf("SHA-512 (FIPS 180-4):\n");
    check_eq(HashStr("sha512", abc),
             "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
             "sha512(abc)");
    check_eq(HashStr("sha512", million_a),
             "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b",
             "sha512(1M x a)");

    std::printf("algorithm name handling:\n");
    check_eq(HashStr("MD5", abc), "900150983cd24fb0d6963f7d28e17f72", "uppercase name accepted");
    check_eq(HashStr("Sha256", abc),
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "mixed-case name accepted");
    {
        std::string out;
        check_int(HashString("crc32", abc, out), 2, "unknown algorithm -> Err_2");
        check_int(HashString("md5", "", out), 3, "empty input -> Err_3");
    }

    std::printf("HashFile:\n");
    {
        const fs::path dir = fs::temp_directory_path() / "zooplug_test_hash";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string file = PathToUTF8(dir) + u8"/ハッシュ対象.txt";
        FileWrite(file, "abc", false); // 中身は正確に "abc"（改行なし）
        std::string out;
        check_int(HashFile("md5", file, out), 0, "hash file ok");
        check_eq(out, "900150983cd24fb0d6963f7d28e17f72", "md5 of file content");
        check_int(HashFile("sha1", PathToUTF8(dir) + "/nope.txt", out), 4, "missing file -> Err_4");
        check_int(HashFile("nope", file, out), 2, "unknown algorithm -> Err_2");
        fs::remove_all(dir, ec);
    }

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

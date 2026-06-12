// HashImpl.cpp — MD5 / SHA-1 / SHA-256 / SHA-512 の自己完結実装
// Part of ZooPlug. License: see License.txt
//
// それぞれ FIPS 180-4 / RFC 1321 の素直なストリーミング実装。
// 定数テーブルは素数の平方根/立方根・sin から高精度計算で生成した値
// （標準仕様と同一）。正しさは tests/test_hash.cpp の NIST ベクタで担保する。

#include "HashImpl.h"

#include "FileOps.h" // PathFromUTF8（日本語パス対応のファイルオープン用）

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace zoo {

namespace {

// ---------------------------------------------------------------------------
// 共通ユーティリティ
// ---------------------------------------------------------------------------

inline std::uint32_t RotL32(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline std::uint32_t RotR32(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline std::uint64_t RotR64(std::uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

std::string ToHex(const unsigned char* digest, std::size_t len) {
    static const char* const hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(hex[digest[i] >> 4]);
        out.push_back(hex[digest[i] & 0x0F]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// MD5 (RFC 1321)
// ---------------------------------------------------------------------------

struct MD5 {
    std::uint32_t h[4] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u };
    std::uint64_t total = 0;
    unsigned char buffer[64];
    std::size_t buffered = 0;

    static const std::uint32_t K[64];
    static const int S[64];

    void Block(const unsigned char* p) {
        std::uint32_t m[16];
        for (int i = 0; i < 16; ++i) {
            m[i] = static_cast<std::uint32_t>(p[i * 4]) |
                   (static_cast<std::uint32_t>(p[i * 4 + 1]) << 8) |
                   (static_cast<std::uint32_t>(p[i * 4 + 2]) << 16) |
                   (static_cast<std::uint32_t>(p[i * 4 + 3]) << 24);
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t f; int g;
            if (i < 16)      { f = (b & c) | (~b & d);        g = i; }
            else if (i < 32) { f = (d & b) | (~d & c);        g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d;                 g = (3 * i + 5) % 16; }
            else             { f = c ^ (b | ~d);              g = (7 * i) % 16; }
            const std::uint32_t tmp = d;
            d = c; c = b;
            b = b + RotL32(a + f + K[i] + m[g], S[i]);
            a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    }

    void Update(const unsigned char* data, std::size_t len) {
        total += len;
        while (len > 0) {
            const std::size_t take = (len < 64 - buffered) ? len : 64 - buffered;
            std::memcpy(buffer + buffered, data, take);
            buffered += take; data += take; len -= take;
            if (buffered == 64) { Block(buffer); buffered = 0; }
        }
    }

    std::string Final() {
        const std::uint64_t bits = total * 8;
        const unsigned char pad = 0x80;
        Update(&pad, 1);
        const unsigned char zero = 0;
        while (buffered != 56) Update(&zero, 1);
        unsigned char len_le[8];
        for (int i = 0; i < 8; ++i) len_le[i] = static_cast<unsigned char>(bits >> (8 * i));
        Update(len_le, 8);
        unsigned char digest[16];
        for (int i = 0; i < 4; ++i) {
            digest[i * 4]     = static_cast<unsigned char>(h[i]);
            digest[i * 4 + 1] = static_cast<unsigned char>(h[i] >> 8);
            digest[i * 4 + 2] = static_cast<unsigned char>(h[i] >> 16);
            digest[i * 4 + 3] = static_cast<unsigned char>(h[i] >> 24);
        }
        return ToHex(digest, 16);
    }
};

const std::uint32_t MD5::K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,
};

const int MD5::S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

// ---------------------------------------------------------------------------
// SHA-1 (FIPS 180-4)
// ---------------------------------------------------------------------------

struct SHA1 {
    std::uint32_t h[5] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u };
    std::uint64_t total = 0;
    unsigned char buffer[64];
    std::size_t buffered = 0;

    void Block(const unsigned char* p) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(p[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = RotL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5a827999u; }
            else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ed9eba1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8f1bbcdcu; }
            else             { f = b ^ c ^ d;                    k = 0xca62c1d6u; }
            const std::uint32_t tmp = RotL32(a, 5) + f + e + k + w[i];
            e = d; d = c; c = RotL32(b, 30); b = a; a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    void Update(const unsigned char* data, std::size_t len) {
        total += len;
        while (len > 0) {
            const std::size_t take = (len < 64 - buffered) ? len : 64 - buffered;
            std::memcpy(buffer + buffered, data, take);
            buffered += take; data += take; len -= take;
            if (buffered == 64) { Block(buffer); buffered = 0; }
        }
    }

    std::string Final() {
        const std::uint64_t bits = total * 8;
        const unsigned char pad = 0x80;
        Update(&pad, 1);
        const unsigned char zero = 0;
        while (buffered != 56) Update(&zero, 1);
        unsigned char len_be[8];
        for (int i = 0; i < 8; ++i) len_be[i] = static_cast<unsigned char>(bits >> (8 * (7 - i)));
        Update(len_be, 8);
        unsigned char digest[20];
        for (int i = 0; i < 5; ++i) {
            digest[i * 4]     = static_cast<unsigned char>(h[i] >> 24);
            digest[i * 4 + 1] = static_cast<unsigned char>(h[i] >> 16);
            digest[i * 4 + 2] = static_cast<unsigned char>(h[i] >> 8);
            digest[i * 4 + 3] = static_cast<unsigned char>(h[i]);
        }
        return ToHex(digest, 20);
    }
};

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4)
// ---------------------------------------------------------------------------

struct SHA256 {
    std::uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    std::uint64_t total = 0;
    unsigned char buffer[64];
    std::size_t buffered = 0;

    static const std::uint32_t K[64];

    void Block(const unsigned char* p) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(p[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = RotR32(w[i - 15], 7) ^ RotR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = RotR32(w[i - 2], 17) ^ RotR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = RotR32(e, 6) ^ RotR32(e, 11) ^ RotR32(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            const std::uint32_t S0 = RotR32(a, 2) ^ RotR32(a, 13) ^ RotR32(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void Update(const unsigned char* data, std::size_t len) {
        total += len;
        while (len > 0) {
            const std::size_t take = (len < 64 - buffered) ? len : 64 - buffered;
            std::memcpy(buffer + buffered, data, take);
            buffered += take; data += take; len -= take;
            if (buffered == 64) { Block(buffer); buffered = 0; }
        }
    }

    std::string Final() {
        const std::uint64_t bits = total * 8;
        const unsigned char pad = 0x80;
        Update(&pad, 1);
        const unsigned char zero = 0;
        while (buffered != 56) Update(&zero, 1);
        unsigned char len_be[8];
        for (int i = 0; i < 8; ++i) len_be[i] = static_cast<unsigned char>(bits >> (8 * (7 - i)));
        Update(len_be, 8);
        unsigned char digest[32];
        for (int i = 0; i < 8; ++i) {
            digest[i * 4]     = static_cast<unsigned char>(h[i] >> 24);
            digest[i * 4 + 1] = static_cast<unsigned char>(h[i] >> 16);
            digest[i * 4 + 2] = static_cast<unsigned char>(h[i] >> 8);
            digest[i * 4 + 3] = static_cast<unsigned char>(h[i]);
        }
        return ToHex(digest, 32);
    }
};

const std::uint32_t SHA256::K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

// ---------------------------------------------------------------------------
// SHA-512 (FIPS 180-4)
// ---------------------------------------------------------------------------

struct SHA512 {
    std::uint64_t h[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
    };
    std::uint64_t total = 0; // バイト数（2^64 バイト超は扱わない）
    unsigned char buffer[128];
    std::size_t buffered = 0;

    static const std::uint64_t K[80];

    void Block(const unsigned char* p) {
        std::uint64_t w[80];
        for (int i = 0; i < 16; ++i) {
            std::uint64_t v = 0;
            for (int b = 0; b < 8; ++b) v = (v << 8) | p[i * 8 + b];
            w[i] = v;
        }
        for (int i = 16; i < 80; ++i) {
            const std::uint64_t s0 = RotR64(w[i - 15], 1) ^ RotR64(w[i - 15], 8) ^ (w[i - 15] >> 7);
            const std::uint64_t s1 = RotR64(w[i - 2], 19) ^ RotR64(w[i - 2], 61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint64_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint64_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; ++i) {
            const std::uint64_t S1 = RotR64(e, 14) ^ RotR64(e, 18) ^ RotR64(e, 41);
            const std::uint64_t ch = (e & f) ^ (~e & g);
            const std::uint64_t t1 = hh + S1 + ch + K[i] + w[i];
            const std::uint64_t S0 = RotR64(a, 28) ^ RotR64(a, 34) ^ RotR64(a, 39);
            const std::uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint64_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void Update(const unsigned char* data, std::size_t len) {
        total += len;
        while (len > 0) {
            const std::size_t take = (len < 128 - buffered) ? len : 128 - buffered;
            std::memcpy(buffer + buffered, data, take);
            buffered += take; data += take; len -= take;
            if (buffered == 128) { Block(buffer); buffered = 0; }
        }
    }

    std::string Final() {
        const std::uint64_t bits = total * 8;
        const unsigned char pad = 0x80;
        Update(&pad, 1);
        const unsigned char zero = 0;
        while (buffered != 112) Update(&zero, 1);
        // 長さフィールドは 128 ビットだが、上位 64 ビットは常に 0 で足りる
        unsigned char len_be[16] = {};
        for (int i = 0; i < 8; ++i) len_be[8 + i] = static_cast<unsigned char>(bits >> (8 * (7 - i)));
        Update(len_be, 16);
        unsigned char digest[64];
        for (int i = 0; i < 8; ++i) {
            for (int b = 0; b < 8; ++b) {
                digest[i * 8 + b] = static_cast<unsigned char>(h[i] >> (8 * (7 - b)));
            }
        }
        return ToHex(digest, 64);
    }
};

const std::uint64_t SHA512::K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
    0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
    0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL,
    0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
    0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL,
    0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
    0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL,
    0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL,
    0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
    0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
    0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

// ---------------------------------------------------------------------------
// ディスパッチ
// ---------------------------------------------------------------------------

enum class Algo { Unknown, Md5, Sha1, Sha256, Sha512 };

Algo ParseAlgo(const std::string& name) {
    std::string folded;
    folded.reserve(name.size());
    for (const char c : name) {
        folded.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    }
    if (folded == "md5") return Algo::Md5;
    if (folded == "sha1") return Algo::Sha1;
    if (folded == "sha256") return Algo::Sha256;
    if (folded == "sha512") return Algo::Sha512;
    return Algo::Unknown;
}

// Update/Final をアルゴリズムごとに呼び分けて 1 本のストリームを処理する
template <typename Source>
int HashStream(Algo algo, Source&& feed, std::string& hex_out) {
    switch (algo) {
        case Algo::Md5:    { MD5 ctx;    if (!feed(ctx)) return 5; hex_out = ctx.Final(); return 0; }
        case Algo::Sha1:   { SHA1 ctx;   if (!feed(ctx)) return 5; hex_out = ctx.Final(); return 0; }
        case Algo::Sha256: { SHA256 ctx; if (!feed(ctx)) return 5; hex_out = ctx.Final(); return 0; }
        case Algo::Sha512: { SHA512 ctx; if (!feed(ctx)) return 5; hex_out = ctx.Final(); return 0; }
        default: return 2;
    }
}

} // namespace

int HashString(const std::string& algorithm, const std::string& text, std::string& hex_out) {
    hex_out.clear();
    const Algo algo = ParseAlgo(algorithm);
    if (algo == Algo::Unknown) return 2;
    if (text.empty()) return 3;
    return HashStream(algo, [&](auto& ctx) {
        ctx.Update(reinterpret_cast<const unsigned char*>(text.data()), text.size());
        return true;
    }, hex_out);
}

int HashFile(const std::string& algorithm, const std::string& file_utf8, std::string& hex_out) {
    hex_out.clear();
    const Algo algo = ParseAlgo(algorithm);
    if (algo == Algo::Unknown) return 2;
    if (file_utf8.empty()) return 3;
    std::error_code ec;
    if (!std::filesystem::exists(PathFromUTF8(file_utf8), ec)) return 4;
    std::ifstream in(PathFromUTF8(file_utf8), std::ios::binary);
    if (!in) return 5;
    return HashStream(algo, [&](auto& ctx) {
        char chunk[64 * 1024];
        while (in) {
            in.read(chunk, sizeof chunk);
            const std::streamsize got = in.gcount();
            if (got > 0) ctx.Update(reinterpret_cast<const unsigned char*>(chunk), static_cast<std::size_t>(got));
        }
        return !in.bad();
    }, hex_out);
}

} // namespace zoo

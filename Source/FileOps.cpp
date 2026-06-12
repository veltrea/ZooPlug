// FileOps.cpp — MooPlug File/Folder 関数群の純粋ロジック実装
// Part of ZooPlug. License: see License.txt
//
// 方針:
//   - すべて std::filesystem の error_code 版を使い、例外を漏らさない。
//   - パスは PathFromUTF8 経由でのみ作る（Windows の CP932 解釈・ダメ文字対策）。
//   - 戻り値の int は MooPlug のエラー番号（0 = 成功）。番号の意味は FileOps.h と
//     docs/mooplug-reference.md を参照。

#include "FileOps.h"

#include "ShellExec.h" // NormalizeNewlines を再利用

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #pragma comment(lib, "Version.lib") // GetFileVersionInfoW / VerQueryValueW
#else
  #include <sys/stat.h>
  #include <sys/time.h>
  #include <fcntl.h>
  #include <unistd.h>
  #if defined(__APPLE__)
    #include <sys/attr.h>
  #endif
#endif

namespace fs = std::filesystem;

namespace zoo {

namespace {

#if defined(_WIN32)

std::wstring BytesToWide(const std::string& s, UINT codepage) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(codepage, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(codepage, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

#endif // _WIN32

// 厳密な UTF-8 妥当性検査（継続バイト・冗長表現・サロゲート・U+10FFFF 超を弾く）
bool IsValidUTF8(const std::string& s) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.data());
    const unsigned char* end = p + s.size();
    while (p < end) {
        const unsigned char c = *p;
        if (c < 0x80) { ++p; continue; }
        int len; unsigned cp_min;
        if ((c & 0xE0) == 0xC0)      { len = 2; cp_min = 0x80; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp_min = 0x800; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp_min = 0x10000; }
        else return false;
        if (end - p < len) return false;
        unsigned cp = c & (0xFF >> (len + 1));
        for (int i = 1; i < len; ++i) {
            if ((p[i] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        if (cp < cp_min) return false;                  // 冗長表現
        if (cp >= 0xD800 && cp <= 0xDFFF) return false; // サロゲート
        if (cp > 0x10FFFF) return false;
        p += len;
    }
    return true;
}

// UTF-8 の先頭バイトからその文字のバイト長を返す（不正なら 1）
std::size_t Utf8CharLen(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

char FoldAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool LessCaseInsensitive(const std::string& a, const std::string& b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        // char は符号付きのことがあるので unsigned で比較する
        //（マルチバイト UTF-8（≥0x80）が ASCII より前に来てしまうのを防ぐ）
        const unsigned char ca = static_cast<unsigned char>(FoldAscii(a[i]));
        const unsigned char cb = static_cast<unsigned char>(FoldAscii(b[i]));
        if (ca != cb) return ca < cb;
    }
    return a.size() < b.size();
}

// FileMaker の CR 改行を OS ネイティブ改行へ（FileWrite 用）
std::string ToNativeNewlines(const std::string& text) {
    // まず CR / CRLF / LF をすべて LF に揃える
    std::string lf;
    lf.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            lf.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            lf.push_back(c);
        }
    }
#if defined(_WIN32)
    std::string out;
    out.reserve(lf.size() + lf.size() / 16);
    for (const char c : lf) {
        if (c == '\n') out += "\r\n";
        else out.push_back(c);
    }
    return out;
#else
    return lf;
#endif
}

} // namespace

fs::path PathFromUTF8(const std::string& utf8) {
    // C++17 の u8path は UTF-8 を正しくネイティブ表現（Win は UTF-16）へ変換する。
    // fs::path(utf8) と書くと Windows で ANSI(CP932) 解釈になり日本語パスが壊れる。
    return fs::u8path(utf8);
}

std::string PathToUTF8(const fs::path& p) {
    return p.u8string();
}

// ---- File ----

int FileExists(const std::string& file_utf8, bool& exists_out) {
    exists_out = false;
    if (file_utf8.empty()) return 2;
    std::error_code ec;
    exists_out = fs::exists(PathFromUTF8(file_utf8), ec);
    return 0;
}

int FileCopy(const std::string& src_utf8, const std::string& dest_utf8, bool overwrite) {
    if (src_utf8.empty()) return 2;
    std::error_code ec;
    const fs::path src = PathFromUTF8(src_utf8);
    if (!fs::exists(src, ec)) return 3;
    if (dest_utf8.empty()) return 4;
    const fs::path dest = PathFromUTF8(dest_utf8);
    if (fs::exists(dest, ec)) {
        if (!overwrite) return 5;
        std::error_code rec;
        if (!fs::remove(dest, rec)) return 7;
    }
    std::error_code cec;
    if (!fs::copy_file(src, dest, cec)) return 6;
    return 0;
}

int FileDelete(const std::string& file_utf8) {
    if (file_utf8.empty()) return 2;
    std::error_code ec;
    const fs::path p = PathFromUTF8(file_utf8);
    if (!fs::exists(p, ec)) return 4;
    std::error_code rec;
    if (!fs::remove(p, rec)) return 3;
    return 0;
}

int FileMove(const std::string& src_utf8, const std::string& dest_utf8, bool overwrite) {
    if (src_utf8.empty()) return 2;
    std::error_code ec;
    const fs::path src = PathFromUTF8(src_utf8);
    if (!fs::exists(src, ec)) return 3;
    if (dest_utf8.empty()) return 4;
    const fs::path dest = PathFromUTF8(dest_utf8);
    if (fs::exists(dest, ec)) {
        if (!overwrite) return 5;
        std::error_code rec;
        if (!fs::remove(dest, rec)) return 7;
    }
    std::error_code mec;
    fs::rename(src, dest, mec);
    if (mec) {
        // 別ドライブ・別デバイスへの rename は失敗するので copy + delete で代替する
        std::error_code cec;
        if (!fs::copy_file(src, dest, cec)) return 6;
        std::error_code dec;
        if (!fs::remove(src, dec)) return 6;
    }
    return 0;
}

int FileRead(const std::string& file_utf8, std::string& text_out) {
    text_out.clear();
    if (file_utf8.empty()) return 2;
    std::ifstream in(PathFromUTF8(file_utf8), std::ios::binary);
    if (!in) return 3;
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) return 5;
    text_out = DecodeFileText(bytes);
    return 0;
}

int FileWrite(const std::string& file_utf8, const std::string& text_utf8, bool append) {
    if (file_utf8.empty()) return 2;
    const fs::path p = PathFromUTF8(file_utf8);
    std::error_code ec;
    if (fs::exists(p, ec) && !append) return 3; // 上書きは不可（0.4.9 実機に bOverwrite は無い）
    std::ofstream out(p, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!out) return 4;
    const std::string native = ToNativeNewlines(text_utf8);
    out.write(native.data(), static_cast<std::streamsize>(native.size()));
    out.close();
    if (out.fail()) return 5;
    return 0;
}

std::string DecodeFileText(const std::string& bytes) {
    std::string s = bytes;
    if (s.size() >= 3 && s.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        s.erase(0, 3); // UTF-8 BOM
    }
    if (!IsValidUTF8(s)) {
#if defined(_WIN32)
        // UTF-8 でないテキストはシステム ANSI コードページ（日本語環境 = CP932）
        // とみなして復号する。レガシーな「ANSI テキスト」の多数派に当たる。
        s = WideToUtf8(BytesToWide(s, CP_ACP));
#endif
        // POSIX では実用的なフォールバック先が無いのでそのまま返す
    }
    return NormalizeNewlines(s);
}

// ---- Moo_FileInfo 用 ----

int FileSize(const std::string& file_utf8, std::uint64_t& bytes_out) {
    bytes_out = 0;
    if (file_utf8.empty()) return 2;
    const fs::path p = PathFromUTF8(file_utf8);
    std::error_code ec;
    if (!fs::exists(p, ec)) return 3;
    std::error_code sec;
    const std::uintmax_t size = fs::file_size(p, sec);
    if (sec) return 6;
    bytes_out = static_cast<std::uint64_t>(size);
    return 0;
}

std::string HumanReadableSize(std::uint64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " bytes";
    }
    static const char* const units[] = { "KB", "MB", "GB", "TB", "PB", "EB" };
    double value = static_cast<double>(bytes);
    int unit = -1;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    // StrFormatByteSize と同じく有効 3 桁・切り捨て（2^31-1 → "1.99 GB" の流儀）
    char buf[64];
    if (value < 10.0) {
        std::snprintf(buf, sizeof buf, "%.2f %s", std::floor(value * 100.0) / 100.0, units[unit]);
    } else if (value < 100.0) {
        std::snprintf(buf, sizeof buf, "%.1f %s", std::floor(value * 10.0) / 10.0, units[unit]);
    } else {
        std::snprintf(buf, sizeof buf, "%d %s", static_cast<int>(value), units[unit]);
    }
    return buf;
}

#if defined(_WIN32)

int FileTimeGet(const std::string& file_utf8, bool creation, FileTimeParts& out) {
    out = FileTimeParts();
    if (file_utf8.empty()) return 2;
    const fs::path p = PathFromUTF8(file_utf8);
    std::error_code ec;
    if (!fs::exists(p, ec)) return 3;

    const HANDLE h = ::CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 6;
    FILETIME ft_create, ft_write;
    const BOOL ok = ::GetFileTime(h, &ft_create, nullptr, &ft_write);
    ::CloseHandle(h);
    if (!ok) return 6;

    SYSTEMTIME utc, local;
    if (!::FileTimeToSystemTime(creation ? &ft_create : &ft_write, &utc)) return 6;
    if (!::SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return 6;
    out.year = local.wYear; out.month = local.wMonth; out.day = local.wDay;
    out.hour = local.wHour; out.minute = local.wMinute; out.second = local.wSecond;
    return 0;
}

int FileTimeSet(const std::string& file_utf8, bool creation, const FileTimeParts& t) {
    if (file_utf8.empty()) return 2;
    const fs::path p = PathFromUTF8(file_utf8);
    std::error_code ec;
    if (!fs::exists(p, ec)) return 3;

    SYSTEMTIME local = {};
    local.wYear = static_cast<WORD>(t.year); local.wMonth = static_cast<WORD>(t.month);
    local.wDay = static_cast<WORD>(t.day);   local.wHour = static_cast<WORD>(t.hour);
    local.wMinute = static_cast<WORD>(t.minute); local.wSecond = static_cast<WORD>(t.second);
    SYSTEMTIME utc;
    if (!::TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc)) return 7;
    FILETIME ft;
    if (!::SystemTimeToFileTime(&utc, &ft)) return 7;

    const HANDLE h = ::CreateFileW(p.c_str(), FILE_WRITE_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 7;
    const BOOL ok = ::SetFileTime(h, creation ? &ft : nullptr, nullptr, creation ? nullptr : &ft);
    ::CloseHandle(h);
    return ok ? 0 : 7;
}

int FileVersion(const std::string& file_utf8, std::string& version_out) {
    version_out.clear();
    if (file_utf8.empty()) return 2;
    const fs::path p = PathFromUTF8(file_utf8);
    std::error_code ec;
    if (!fs::exists(p, ec)) return 3;

    DWORD handle = 0;
    const DWORD size = ::GetFileVersionInfoSizeW(p.c_str(), &handle);
    if (size == 0) return 6;
    std::vector<char> buffer(size);
    if (!::GetFileVersionInfoW(p.c_str(), 0, size, buffer.data())) return 6;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!::VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &len) || !info) return 6;

    char buf[64];
    // 本家と同じ "%d.%d.%d.%d" 形式（FileVersion の固定情報から）
    std::snprintf(buf, sizeof buf, "%u.%u.%u.%u",
                  HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                  HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    version_out = buf;
    return 0;
}

#else // POSIX

int FileTimeGet(const std::string& file_utf8, bool creation, FileTimeParts& out) {
    out = FileTimeParts();
    if (file_utf8.empty()) return 2;
    const fs::path p = PathFromUTF8(file_utf8);
    std::error_code ec;
    if (!fs::exists(p, ec)) return 3;

    struct stat st;
    if (::stat(p.c_str(), &st) != 0) return 6;
    time_t when;
    if (creation) {
#if defined(__APPLE__)
        when = st.st_birthtimespec.tv_sec;
#else
        return 6; // Linux の stat は作成日時を持たない
#endif
    } else {
#if defined(__APPLE__)
        when = st.st_mtimespec.tv_sec;
#else
        when = st.st_mtim.tv_sec;
#endif
    }
    struct tm parts;
    if (::localtime_r(&when, &parts) == nullptr) return 6;
    out.year = parts.tm_year + 1900; out.month = parts.tm_mon + 1; out.day = parts.tm_mday;
    out.hour = parts.tm_hour; out.minute = parts.tm_min; out.second = parts.tm_sec;
    return 0;
}

int FileTimeSet(const std::string& file_utf8, bool creation, const FileTimeParts& t) {
    if (file_utf8.empty()) return 2;
    const fs::path p = PathFromUTF8(file_utf8);
    std::error_code ec;
    if (!fs::exists(p, ec)) return 3;

    struct tm parts = {};
    parts.tm_year = t.year - 1900; parts.tm_mon = t.month - 1; parts.tm_mday = t.day;
    parts.tm_hour = t.hour; parts.tm_min = t.minute; parts.tm_sec = t.second;
    parts.tm_isdst = -1;
    const time_t when = ::mktime(&parts);
    if (when == static_cast<time_t>(-1)) return 7;

    if (creation) {
#if defined(__APPLE__)
        struct attrlist attrs = {};
        attrs.bitmapcount = ATTR_BIT_MAP_COUNT;
        attrs.commonattr = ATTR_CMN_CRTIME;
        struct timespec crtime = {};
        crtime.tv_sec = when;
        if (::setattrlist(p.c_str(), &attrs, &crtime, sizeof crtime, 0) != 0) return 7;
        return 0;
#else
        return 7; // Linux は作成日時を設定できない
#endif
    }

    struct timespec times[2];
    times[0].tv_nsec = UTIME_OMIT; // atime は触らない
    times[0].tv_sec = 0;
    times[1].tv_sec = when;
    times[1].tv_nsec = 0;
    if (::utimensat(AT_FDCWD, p.c_str(), times, 0) != 0) return 7;
    return 0;
}

int FileVersion(const std::string& file_utf8, std::string& version_out) {
    version_out.clear();
    if (file_utf8.empty()) return 2;
    std::error_code ec;
    if (!fs::exists(PathFromUTF8(file_utf8), ec)) return 3;
    return 6; // バージョンリソースは Windows 専用（GetFileVersionInfo）
}

#endif // _WIN32 / POSIX

// ---- Folder ----

int FolderExists(const std::string& folder_utf8, bool& exists_out) {
    exists_out = false;
    if (folder_utf8.empty()) return 2;
    std::error_code ec;
    exists_out = fs::is_directory(PathFromUTF8(folder_utf8), ec);
    return 0;
}

int FolderCopy(const std::string& src_utf8, const std::string& dest_utf8) {
    if (src_utf8.empty()) return 2;
    std::error_code ec;
    const fs::path src = PathFromUTF8(src_utf8);
    if (!fs::is_directory(src, ec)) return 3;
    if (dest_utf8.empty()) return 4;
    const fs::path dest = PathFromUTF8(dest_utf8);
    if (fs::exists(dest, ec)) return 5;
    std::error_code cec;
    fs::copy(src, dest, fs::copy_options::recursive, cec);
    if (cec) return 6;
    return 0;
}

int FolderCreate(const std::string& folder_utf8) {
    if (folder_utf8.empty()) return 2;
    const fs::path p = PathFromUTF8(folder_utf8);
    std::error_code ec;
    if (fs::exists(p, ec)) return 3;
    std::error_code cec;
    if (!fs::create_directories(p, cec)) return 4;
    return 0;
}

int FolderDelete(const std::string& folder_utf8) {
    if (folder_utf8.empty()) return 2;
    const fs::path p = PathFromUTF8(folder_utf8);
    std::error_code ec;
    if (!fs::exists(p, ec)) return 3;
    std::error_code rec;
    fs::remove_all(p, rec);
    if (rec) return 4;
    return 0;
}

int FolderMove(const std::string& src_utf8, const std::string& dest_utf8) {
    if (src_utf8.empty()) return 2;
    std::error_code ec;
    const fs::path src = PathFromUTF8(src_utf8);
    if (!fs::is_directory(src, ec)) return 3;
    if (dest_utf8.empty()) return 4;
    const fs::path dest = PathFromUTF8(dest_utf8);
    if (fs::exists(dest, ec)) return 5;
    std::error_code mec;
    fs::rename(src, dest, mec);
    if (mec) {
        // 別ドライブへの rename は失敗するので copy + delete で代替する
        std::error_code cec;
        fs::copy(src, dest, fs::copy_options::recursive, cec);
        if (cec) return 6;
        std::error_code dec;
        fs::remove_all(src, dec);
        if (dec) return 6;
    }
    return 0;
}

int FolderList(const std::string& folder_utf8, const std::string& pattern_utf8,
               const std::string& separator_utf8, std::string& list_out) {
    list_out.clear();
    if (folder_utf8.empty()) return 2;
    const fs::path dir = PathFromUTF8(folder_utf8);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 4;

    std::vector<std::string> names;
    std::error_code iec;
    fs::directory_iterator it(dir, iec);
    if (iec) return 5;
    for (const fs::directory_entry& entry : it) {
        std::error_code fec;
        if (!entry.is_regular_file(fec)) continue; // フォルダ等は出さない（ファイルのみ）
        const std::string name = PathToUTF8(entry.path().filename());
        if (WildcardMatch(name, pattern_utf8)) {
            names.push_back(name);
        }
    }
    if (names.empty()) return 3;

    // directory_iterator の列挙順は未規定なので、FindFirstFile(NTFS) と同じく
    // アルファベット順（ASCII は大文字小文字無視）に揃える
    std::sort(names.begin(), names.end(), LessCaseInsensitive);

    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) list_out += separator_utf8;
        list_out += names[i];
    }
    return 0;
}

bool WildcardMatch(const std::string& name_utf8, const std::string& pattern_utf8) {
    // FindFirstFile の流儀: "*.*" は「すべてのファイル」（拡張子なしも含む）
    if (pattern_utf8.empty() || pattern_utf8 == "*" || pattern_utf8 == "*.*") return true;

    // 反復型 glob 照合（* のバックトラック付き）。比較はバイト単位で行うが、
    // UTF-8 は自己同期するので * / リテラル比較はそのままで正しい。
    // '?' だけは「1 文字 = 1 コードポイント」を消費する。
    const std::string& n = name_utf8;
    const std::string& p = pattern_utf8;
    std::size_t ni = 0, pi = 0;
    std::size_t star = std::string::npos, mark = 0;

    while (ni < n.size()) {
        if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            mark = ni;
        } else if (pi < p.size() && p[pi] == '?') {
            ni += Utf8CharLen(static_cast<unsigned char>(n[ni]));
            ++pi;
        } else if (pi < p.size() && FoldAscii(p[pi]) == FoldAscii(n[ni])) {
            ++ni;
            ++pi;
        } else if (star != std::string::npos) {
            pi = star + 1;
            mark += Utf8CharLen(static_cast<unsigned char>(n[mark]));
            ni = mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

} // namespace zoo

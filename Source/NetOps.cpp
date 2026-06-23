// NetOps.cpp
//
// Download 2 + FTP 3 関数の実装。詳細は NetOps.h を参照。
//   Windows : WinINet（wininet.lib）/ POSIX : libcurl
//
// Part of ZooPlug. License: see License.txt

// windows.h より前に NOMINMAX（min/max マクロ衝突を回避）
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif

#include "NetOps.h"
#include "FileOps.h"   // zoo::PathFromUTF8 / PathToUTF8

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace zoo {

// ===========================================================================
// 共通（非通信）ロジック
// ===========================================================================

namespace {

std::string TrimAscii(const std::string& s)
{
    std::size_t b = 0, e = s.size();
    while (b < e && static_cast<unsigned char>(s[b]) <= ' ') ++b;
    while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') --e;
    return s.substr(b, e - b);
}

bool StartsWithCI(const std::string& s, const char* prefix)
{
    std::size_t i = 0;
    for (; prefix[i]; ++i) {
        if (i >= s.size()) return false;
        char a = s[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// URL の末尾コンポーネント（クエリ・フラグメントを除く）を返す。空なら "download"。
std::string UrlFilename(const std::string& url)
{
    std::string s = url;
    // クエリ・フラグメントを落とす
    std::size_t cut = s.find_first_of("?#");
    if (cut != std::string::npos) s = s.substr(0, cut);
    // 末尾スラッシュを落とす
    while (!s.empty() && s.back() == '/') s.pop_back();
    std::size_t slash = s.find_last_of('/');
    std::string name = (slash == std::string::npos) ? s : s.substr(slash + 1);
    // scheme 部分しか残らなかった等の保険
    if (name.empty() || name.find("://") != std::string::npos) name = "download";
    return name;
}

// DownloadFile の保存先を解決する。0=OK / 5=ローカルフォルダ無し。
int ResolveDownloadTarget(const std::string& url, const std::string& local,
                          fs::path& out)
{
    if (local.empty()) {
        std::error_code ec;
        fs::path tmp = fs::temp_directory_path(ec);
        if (ec) return 5;
        out = tmp / PathFromUTF8(UrlFilename(url));
        return 0;
    }
    fs::path lp = PathFromUTF8(local);
    std::error_code ec;
    if (fs::is_directory(lp, ec)) {
        out = lp / PathFromUTF8(UrlFilename(url));
        return 0;
    }
    // ファイルパス指定: 親フォルダが存在しなければ Err_5
    fs::path parent = lp.parent_path();
    if (!parent.empty() && !fs::exists(parent, ec)) return 5;
    out = lp;
    return 0;
}

struct FtpHost {
    std::string host;
    int port = 0;   // 0 = 既定ポート
};

// "ftp://user@host:21/path" 等から host と port を取り出す（scheme/credential/path は捨てる）。
FtpHost ParseFtpHost(const std::string& server)
{
    std::string s = TrimAscii(server);
    std::size_t scheme = s.find("://");
    if (scheme != std::string::npos) s = s.substr(scheme + 3);
    // credential（user@）を落とす
    std::size_t at = s.find('@');
    if (at != std::string::npos) s = s.substr(at + 1);
    // path を落とす
    std::size_t slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(0, slash);
    FtpHost out;
    std::size_t colon = s.rfind(':');
    if (colon != std::string::npos) {
        std::string p = s.substr(colon + 1);
        bool digits = !p.empty() &&
            std::all_of(p.begin(), p.end(), [](char c){ return c >= '0' && c <= '9'; });
        if (digits) {
            out.port = std::atoi(p.c_str());
            s = s.substr(0, colon);
        }
    }
    out.host = s;
    return out;
}

// FTP のリモートパス先頭のスラッシュを 1 個に正規化（libcurl の ftp:// 連結用）。
std::string TrimLeadingSlashes(const std::string& p)
{
    std::size_t i = 0;
    while (i < p.size() && p[i] == '/') ++i;
    return p.substr(i);
}

} // namespace

bool IsValidHttpUrl(const std::string& url_utf8)
{
    std::string s = TrimAscii(url_utf8);
    if (!(StartsWithCI(s, "http://") || StartsWithCI(s, "https://"))) return false;
    // scheme の後ろに最低 1 文字（ホスト）が要る
    std::size_t scheme = s.find("://");
    return scheme != std::string::npos && s.size() > scheme + 3;
}

int ValidateFtpParams(const FtpParams& ftp)
{
    if (TrimAscii(ftp.server).empty()) return 3;
    if (ftp.user.empty()) return 4;
    if (ftp.password.empty()) return 5;
    return 0;
}

// ===========================================================================
// プラットフォーム依存の通信実装
// ===========================================================================

#ifdef _WIN32

} // namespace zoo  （windows ヘッダはグローバルで取り込む）

#include <windows.h>
#include <wininet.h>

namespace zoo {

namespace {

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// HINTERNET を確実に閉じる小さな RAII
struct InetHandle {
    HINTERNET h = nullptr;
    InetHandle() = default;
    explicit InetHandle(HINTERNET x) : h(x) {}
    ~InetHandle() { if (h) InternetCloseHandle(h); }
    InetHandle(const InetHandle&) = delete;
    InetHandle& operator=(const InetHandle&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

// HTTP GET 本文をメモリに受ける。0=OK / 3=失敗
int HttpGetToBuffer(const std::string& url, std::string& body_out)
{
    InetHandle inet(InternetOpenW(L"ZooPlug/1.0", INTERNET_OPEN_TYPE_PRECONFIG,
                                  nullptr, nullptr, 0));
    if (!inet) return 3;
    InetHandle req(InternetOpenUrlW(inet.h, Utf8ToWide(url).c_str(), nullptr, 0,
                                    INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                                    INTERNET_FLAG_NO_UI, 0));
    if (!req) return 3;

    // HTTP ステータス >= 400 は失敗扱い（FTP URL では取得できないので無視）
    DWORD status = 0, len = sizeof(status), idx = 0;
    if (HttpQueryInfoW(req.h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                       &status, &len, &idx) && status >= 400) {
        return 3;
    }

    body_out.clear();
    char buf[16384];
    DWORD got = 0;
    while (InternetReadFile(req.h, buf, sizeof(buf), &got) && got > 0) {
        body_out.append(buf, got);
    }
    return 0;
}

// FTP 接続を確立する。エラー番号を err に入れ、成功なら接続ハンドルを返す（呼び出し側が閉じる）。
// inet も同時に開いて両方を out に返す。
int FtpConnect(const FtpParams& ftp, InetHandle& inet, InetHandle& conn)
{
    inet.h = InternetOpenW(L"ZooPlug/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!inet) return 6;
    FtpHost host = ParseFtpHost(ftp.server);
    INTERNET_PORT port = host.port ? (INTERNET_PORT)host.port : INTERNET_DEFAULT_FTP_PORT;
    conn.h = InternetConnectW(inet.h, Utf8ToWide(host.host).c_str(), port,
                              Utf8ToWide(ftp.user).c_str(), Utf8ToWide(ftp.password).c_str(),
                              INTERNET_SERVICE_FTP, INTERNET_FLAG_PASSIVE, 0);
    if (!conn) return 7;
    return 0;
}

bool FtpRemoteExists(HINTERNET conn, const std::string& remote)
{
    WIN32_FIND_DATAW fd;
    InetHandle find(FtpFindFirstFileW(conn, Utf8ToWide(remote).c_str(), &fd, 0, 0));
    return (bool)find;
}

} // namespace

int DownloadText(const std::string& url_utf8, std::string& text_out)
{
    if (!IsValidHttpUrl(url_utf8)) return 2;
    return HttpGetToBuffer(url_utf8, text_out);
}

int DownloadFile(const std::string& url_utf8, const std::string& local_utf8,
                 std::string& path_out)
{
    if (!IsValidHttpUrl(url_utf8)) return 2;
    fs::path target;
    int e = ResolveDownloadTarget(url_utf8, local_utf8, target);
    if (e) return e;

    std::string body;
    int g = HttpGetToBuffer(url_utf8, body);
    if (g) return g;

    std::ofstream ofs(target, std::ios::binary);
    if (!ofs) return 5;
    ofs.write(body.data(), (std::streamsize)body.size());
    if (!ofs.good()) return 3;
    ofs.close();
    path_out = PathToUTF8(target);
    return 0;
}

int FTPDownload(const FtpParams& ftp, const std::string& remote_path_utf8,
                const std::string& local_file_utf8, std::string& path_out)
{
    int v = ValidateFtpParams(ftp);
    if (v) return v;

    fs::path target;
    if (local_file_utf8.empty()) {
        std::error_code ec;
        fs::path tmp = fs::temp_directory_path(ec);
        if (ec) return 13;
        target = tmp / PathFromUTF8(UrlFilename(remote_path_utf8));
    } else {
        target = PathFromUTF8(local_file_utf8);
        std::error_code ec;
        if (fs::is_directory(target, ec)) {
            target /= PathFromUTF8(UrlFilename(remote_path_utf8));
        }
    }
    std::error_code ec;
    if (fs::exists(target, ec)) return 8;  // ローカルに同名既存

    InetHandle inet, conn;
    int c = FtpConnect(ftp, inet, conn);
    if (c) return c;

    if (!FtpRemoteExists(conn.h, remote_path_utf8)) return 10;

    BOOL ok = FtpGetFileW(conn.h, Utf8ToWide(remote_path_utf8).c_str(),
                          target.wstring().c_str(), FALSE, FILE_ATTRIBUTE_NORMAL,
                          FTP_TRANSFER_TYPE_BINARY, 0);
    if (!ok) return 11;
    path_out = PathToUTF8(target);
    return 0;
}

int FTPUpload(const FtpParams& ftp, const std::string& local_file_utf8,
              const std::string& remote_path_utf8, bool overwrite)
{
    int v = ValidateFtpParams(ftp);
    if (v) return v;

    fs::path local = PathFromUTF8(local_file_utf8);
    std::error_code ec;
    if (!fs::exists(local, ec) || fs::is_directory(local, ec)) return 8;

    InetHandle inet, conn;
    int c = FtpConnect(ftp, inet, conn);
    if (c) return c;

    if (!overwrite && FtpRemoteExists(conn.h, remote_path_utf8)) return 11;

    BOOL ok = FtpPutFileW(conn.h, local.wstring().c_str(),
                          Utf8ToWide(remote_path_utf8).c_str(),
                          FTP_TRANSFER_TYPE_BINARY, 0);
    if (!ok) {
        return (GetLastError() == ERROR_FILE_NOT_FOUND) ? 12 : 16;
    }
    return 0;
}

int FTPDelete(const FtpParams& ftp, const std::string& remote_path_utf8)
{
    int v = ValidateFtpParams(ftp);
    if (v) return v;

    InetHandle inet, conn;
    int c = FtpConnect(ftp, inet, conn);
    if (c) return c;

    BOOL ok = FtpDeleteFileW(conn.h, Utf8ToWide(remote_path_utf8).c_str());
    if (!ok) return 8;
    return 0;
}

#else // ===================== POSIX (libcurl) ==============================

} // namespace zoo

#include <curl/curl.h>

namespace zoo {

namespace {

// libcurl のグローバル初期化を一度だけ行う（easy インターフェースの暗黙初期化は
// スレッド安全でないため、明示的に先回りする）。
struct CurlGlobal {
    CurlGlobal()  { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};
const CurlGlobal g_curl_global;

std::size_t WriteToString(char* ptr, std::size_t size, std::size_t nmemb, void* ud)
{
    std::string* s = static_cast<std::string*>(ud);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

std::size_t WriteToStream(char* ptr, std::size_t size, std::size_t nmemb, void* ud)
{
    std::ofstream* o = static_cast<std::ofstream*>(ud);
    o->write(ptr, (std::streamsize)(size * nmemb));
    return o->good() ? size * nmemb : 0;
}

std::size_t ReadFromStream(char* ptr, std::size_t size, std::size_t nmemb, void* ud)
{
    std::ifstream* i = static_cast<std::ifstream*>(ud);
    i->read(ptr, (std::streamsize)(size * nmemb));
    return (std::size_t)i->gcount();
}

// "ftp://host[:port]/remote" を組み立てる（remote 先頭スラッシュは 1 個に）。
std::string BuildFtpUrl(const FtpParams& ftp, const std::string& remote)
{
    FtpHost h = ParseFtpHost(ftp.server);
    std::string url = "ftp://" + h.host;
    if (h.port) url += ":" + std::to_string(h.port);
    url += "/" + TrimLeadingSlashes(remote);
    return url;
}

void SetFtpAuth(CURL* curl, const FtpParams& ftp)
{
    std::string userpwd = ftp.user + ":" + ftp.password;
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
}

// libcurl の FTP 接続系エラーかどうか（→ Err_7）
int MapFtpConnectError(CURLcode rc)
{
    switch (rc) {
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
        case CURLE_FTP_WEIRD_SERVER_REPLY:
        case CURLE_FTP_ACCEPT_FAILED:
        case CURLE_FTP_ACCEPT_TIMEOUT:
        case CURLE_LOGIN_DENIED:
        case CURLE_OPERATION_TIMEDOUT:
            return 7;
        default:
            return 0;
    }
}

} // namespace

int DownloadText(const std::string& url_utf8, std::string& text_out)
{
    if (!IsValidHttpUrl(url_utf8)) return 2;
    CURL* curl = curl_easy_init();
    if (!curl) return 3;
    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url_utf8.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);     // HTTP >= 400 を失敗に
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ZooPlug/1.0");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) return 3;
    text_out = body;
    return 0;
}

int DownloadFile(const std::string& url_utf8, const std::string& local_utf8,
                 std::string& path_out)
{
    if (!IsValidHttpUrl(url_utf8)) return 2;
    fs::path target;
    int e = ResolveDownloadTarget(url_utf8, local_utf8, target);
    if (e) return e;

    std::ofstream ofs(target, std::ios::binary);
    if (!ofs) return 5;

    CURL* curl = curl_easy_init();
    if (!curl) return 3;
    curl_easy_setopt(curl, CURLOPT_URL, url_utf8.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToStream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ZooPlug/1.0");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    ofs.close();
    if (rc != CURLE_OK) {
        std::error_code ec; fs::remove(target, ec);
        return 3;
    }
    path_out = PathToUTF8(target);
    return 0;
}

int FTPDownload(const FtpParams& ftp, const std::string& remote_path_utf8,
                const std::string& local_file_utf8, std::string& path_out)
{
    int v = ValidateFtpParams(ftp);
    if (v) return v;

    fs::path target;
    if (local_file_utf8.empty()) {
        std::error_code ec;
        fs::path tmp = fs::temp_directory_path(ec);
        if (ec) return 13;
        target = tmp / PathFromUTF8(UrlFilename(remote_path_utf8));
    } else {
        target = PathFromUTF8(local_file_utf8);
        std::error_code ec;
        if (fs::is_directory(target, ec)) {
            target /= PathFromUTF8(UrlFilename(remote_path_utf8));
        }
    }
    std::error_code ec;
    if (fs::exists(target, ec)) return 8;

    std::ofstream ofs(target, std::ios::binary);
    if (!ofs) return 13;

    CURL* curl = curl_easy_init();
    if (!curl) return 7;
    const std::string url = BuildFtpUrl(ftp, remote_path_utf8);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    SetFtpAuth(curl, ftp);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToStream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    ofs.close();
    if (rc != CURLE_OK) {
        fs::remove(target, ec);
        if (rc == CURLE_REMOTE_FILE_NOT_FOUND || rc == CURLE_FTP_COULDNT_RETR_FILE) return 10;
        int conn = MapFtpConnectError(rc);
        return conn ? conn : 11;
    }
    path_out = PathToUTF8(target);
    return 0;
}

int FTPUpload(const FtpParams& ftp, const std::string& local_file_utf8,
              const std::string& remote_path_utf8, bool overwrite)
{
    int v = ValidateFtpParams(ftp);
    if (v) return v;

    fs::path local = PathFromUTF8(local_file_utf8);
    std::error_code ec;
    if (!fs::exists(local, ec) || fs::is_directory(local, ec)) return 8;

    std::ifstream ifs(local, std::ios::binary);
    if (!ifs) return 12;
    const std::uintmax_t size = fs::file_size(local, ec);

    const std::string url = BuildFtpUrl(ftp, remote_path_utf8);

    // 上書き不可なら事前に存在確認（サイズ取得が成功 = 既存）
    if (!overwrite) {
        CURL* probe = curl_easy_init();
        if (probe) {
            curl_easy_setopt(probe, CURLOPT_URL, url.c_str());
            SetFtpAuth(probe, ftp);
            curl_easy_setopt(probe, CURLOPT_NOBODY, 1L);
            curl_easy_setopt(probe, CURLOPT_NOSIGNAL, 1L);
            CURLcode prc = curl_easy_perform(probe);
            curl_easy_cleanup(probe);
            if (prc == CURLE_OK) return 11;   // 既に存在
        }
    }

    CURL* curl = curl_easy_init();
    if (!curl) return 7;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    SetFtpAuth(curl, ftp);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, ReadFromStream);
    curl_easy_setopt(curl, CURLOPT_READDATA, &ifs);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)size);
    curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        int conn = MapFtpConnectError(rc);
        return conn ? conn : 16;
    }
    return 0;
}

int FTPDelete(const FtpParams& ftp, const std::string& remote_path_utf8)
{
    int v = ValidateFtpParams(ftp);
    if (v) return v;

    FtpHost h = ParseFtpHost(ftp.server);
    // DELE はディレクトリ URL + postquote で送る。
    std::string base = "ftp://" + h.host;
    if (h.port) base += ":" + std::to_string(h.port);
    base += "/";

    CURL* curl = curl_easy_init();
    if (!curl) return 7;
    curl_easy_setopt(curl, CURLOPT_URL, base.c_str());
    SetFtpAuth(curl, ftp);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    std::string cmd = "DELE " + TrimLeadingSlashes(remote_path_utf8);
    struct curl_slist* quote = curl_slist_append(nullptr, cmd.c_str());
    curl_easy_setopt(curl, CURLOPT_QUOTE, quote);

    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(quote);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        int conn = MapFtpConnectError(rc);
        return conn ? conn : 8;
    }
    return 0;
}

#endif // _WIN32 / POSIX

} // namespace zoo

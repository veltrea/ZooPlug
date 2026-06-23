// PrinterOps.cpp
//
// Printer 2 関数の実装。詳細は PrinterOps.h を参照。
//   Windows : winspool / POSIX : CUPS
//
// Part of ZooPlug. License: see License.txt

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#endif

#include "PrinterOps.h"

#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  include <winspool.h>
#else
#  include <cups/cups.h>
#endif

namespace zoo {

#ifdef _WIN32

namespace {

std::string WideToUtf8(const wchar_t* w)
{
    if (!w || !*w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

} // namespace

int PrinterGetDefault(std::string& name_out)
{
    DWORD size = 0;
    GetDefaultPrinterW(nullptr, &size);   // 必要サイズ（文字数・null 含む）を得る
    if (size == 0) return 2;
    std::wstring buf(size, L'\0');
    if (!GetDefaultPrinterW(&buf[0], &size)) return 2;
    if (size > 0 && buf.size() >= size) buf.resize(size - 1); // 末尾 null を落とす
    name_out = WideToUtf8(buf.c_str());
    if (name_out.empty()) return 2;
    return 0;
}

int PrinterSetDefault(const std::string& name_utf8)
{
    if (!SetDefaultPrinterW(Utf8ToWide(name_utf8).c_str())) return 3;
    return 0;
}

int PrinterList(const std::string& separator_utf8, std::string& list_out)
{
    const DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    DWORD needed = 0, returned = 0;
    EnumPrintersW(flags, nullptr, 4, nullptr, 0, &needed, &returned);
    if (needed == 0) return 3;   // 1 台も無い
    std::vector<BYTE> buf(needed);
    if (!EnumPrintersW(flags, nullptr, 4, buf.data(), needed, &needed, &returned)) return 2;
    if (returned == 0) return 3;

    const PRINTER_INFO_4W* info = reinterpret_cast<const PRINTER_INFO_4W*>(buf.data());
    std::string out;
    for (DWORD i = 0; i < returned; ++i) {
        if (i) out += separator_utf8;
        out += WideToUtf8(info[i].pPrinterName);
    }
    list_out = out;
    return 0;
}

#else // ===================== POSIX (CUPS) ==============================

int PrinterGetDefault(std::string& name_out)
{
    cups_dest_t* d = cupsGetNamedDest(CUPS_HTTP_DEFAULT, nullptr, nullptr);
    if (!d) return 2;
    name_out = d->name ? d->name : std::string();
    cupsFreeDests(1, d);
    if (name_out.empty()) return 2;
    return 0;
}

int PrinterSetDefault(const std::string& name_utf8)
{
    cups_dest_t* dests = nullptr;
    const int n = cupsGetDests(&dests);
    cups_dest_t* target = cupsGetDest(name_utf8.c_str(), nullptr, n, dests);
    if (!target) { cupsFreeDests(n, dests); return 3; }
    for (int i = 0; i < n; ++i) dests[i].is_default = 0;
    target->is_default = 1;
    cupsSetDests(n, dests);   // ~/.cups/lpoptions にユーザー既定を書く
    cupsFreeDests(n, dests);
    return 0;
}

int PrinterList(const std::string& separator_utf8, std::string& list_out)
{
    cups_dest_t* dests = nullptr;
    const int n = cupsGetDests(&dests);
    if (n <= 0) { if (dests) cupsFreeDests(n, dests); return 3; }
    std::string out;
    for (int i = 0; i < n; ++i) {
        if (i) out += separator_utf8;
        if (dests[i].name) out += dests[i].name;
    }
    cupsFreeDests(n, dests);
    list_out = out;
    return 0;
}

#endif // _WIN32 / POSIX

} // namespace zoo

// DialogOps.cpp
//
// Dialog 3 関数の Windows 実装と Linux スタブ。詳細は DialogOps.h を参照。
// macOS の実装は DialogOps_mac.mm（AppKit）にあり、このファイルは __APPLE__ では空になる。
//
// Part of ZooPlug. License: see License.txt

#if defined(_WIN32)

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif

#include "DialogOps.h"

#include <cstdio>
#include <string>
#include <vector>

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

namespace zoo {

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

int DialogColour(bool full, std::string& colour_out)
{
    static COLORREF custom[16] = {0};
    CHOOSECOLORW cc;
    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize  = sizeof(cc);
    cc.lpCustColors = custom;
    cc.rgbResult    = RGB(0, 0, 0);
    cc.Flags        = CC_RGBINIT | CC_ANYCOLOR | (full ? CC_FULLOPEN : 0);
    if (!ChooseColorW(&cc)) return 2;   // キャンセル（またはエラー）
    const COLORREF c = cc.rgbResult;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02X%02X%02X",
                  GetRValue(c), GetGValue(c), GetBValue(c));
    colour_out = buf;
    return 0;
}

int DialogFile(bool open, const std::string& title, const std::string& default_name,
               std::string& path_out)
{
    std::vector<wchar_t> file(32768, L'\0');   // 長いパスにも耐える
    const std::wstring def = Utf8ToWide(default_name);
    if (!def.empty()) {
        const std::size_t n = def.size() < file.size() - 1 ? def.size() : file.size() - 1;
        std::copy(def.begin(), def.begin() + n, file.begin());
    }
    const std::wstring wtitle = Utf8ToWide(title);

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile   = file.data();
    ofn.nMaxFile    = static_cast<DWORD>(file.size());
    ofn.lpstrTitle  = wtitle.empty() ? nullptr : wtitle.c_str();
    ofn.Flags       = OFN_NOCHANGEDIR | OFN_EXPLORER |
                      (open ? (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST) : OFN_OVERWRITEPROMPT);

    const BOOL ok = open ? GetOpenFileNameW(&ofn) : GetSaveFileNameW(&ofn);
    if (!ok) return 2;   // キャンセル
    path_out = WideToUtf8(file.data());
    return 0;
}

int DialogFolder(const std::string& title, bool new_folder, std::string& path_out)
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool inited = SUCCEEDED(hr);

    const std::wstring wtitle = Utf8ToWide(title);
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.lpszTitle = wtitle.empty() ? nullptr : wtitle.c_str();
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE |
                   (new_folder ? 0 : BIF_NONEWFOLDERBUTTON);

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        if (inited) CoUninitialize();
        return 2;   // キャンセル
    }
    wchar_t path[MAX_PATH] = {0};
    const BOOL got = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    if (inited) CoUninitialize();
    if (!got) return 3;   // 取得失敗
    path_out = WideToUtf8(path);
    return 0;
}

} // namespace zoo

#elif !defined(__APPLE__)

// Linux（FileMaker Server）: GUI が無いのでダイアログは出せない。キャンセル扱い（Err_2）。
#include "DialogOps.h"

namespace zoo {

int DialogColour(bool, std::string&) { return 2; }
int DialogFile(bool, const std::string&, const std::string&, std::string&) { return 2; }
int DialogFolder(const std::string&, bool, std::string&) { return 2; }

} // namespace zoo

#endif // _WIN32 / (!__APPLE__)  ── __APPLE__ では DialogOps_mac.mm が実装する

// HotkeyOps.cpp
//
// Hotkey 3 関数の実装。詳細は HotkeyOps.h を参照。
//   Windows : メッセージ専用ウィンドウ + RegisterHotKey（FileMaker のメッセージポンプが
//             WM_HOTKEY を配送）。非グローバルは前面が自プロセスのときだけ発火。
//   macOS   : Carbon RegisterEventHotKey + メイン run loop のイベントハンドラ。
//             RegisterEventHotKey は本来システム全体なので bGlobal は無視（TODO-compat）。
//   Linux   : グローバルホットキー非対応（FileMaker Server・GUI 無し）→ Err_3。
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

#include "HotkeyOps.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#elif defined(__APPLE__)
#  include <Carbon/Carbon.h>
#endif

namespace zoo {

namespace {

// ---- 共通の状態（mutex 保護） ----
struct Binding {
    std::string canonical;   // 正規キー名（レジストリのキー）
    std::string display;     // シグネチャ（"CTRL+SHIFT+A"）
    std::string file, script, param;
    bool alt = false, ctrl = false, shift = false, global = false;
    int  os_id = 0;          // OS 登録 id（RegisterHotKey id / EventHotKeyID.id）
};

std::mutex                       g_mutex;
std::map<std::string, Binding>   g_registry;     // canonical -> Binding
std::map<int, std::string>       g_id_to_key;    // os_id -> canonical
std::deque<HotkeyFire>           g_fire_queue;
int                              g_next_id = 1;

std::string TrimUpper(const std::string& s)
{
    std::size_t b = 0, e = s.size();
    while (b < e && static_cast<unsigned char>(s[b]) <= ' ') ++b;
    while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') --e;
    std::string out = s.substr(b, e - b);
    for (char& c : out) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

bool IsKnownToken(const std::string& t)
{
    if (t.size() == 1) {
        const char c = t[0];
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }
    if (t.size() >= 2 && t[0] == 'F') {
        // F1..F12
        bool digits = true;
        for (std::size_t i = 1; i < t.size(); ++i) if (t[i] < '0' || t[i] > '9') { digits = false; break; }
        if (digits) {
            const int n = std::atoi(t.c_str() + 1);
            return n >= 1 && n <= 12;
        }
    }
    return t == "SPACE" || t == "ESC" || t == "END" || t == "HOME" ||
           t == "UP" || t == "DOWN" || t == "LEFT" || t == "RIGHT";
}

// ---- プラットフォーム層（前方宣言。定義は下の #ifdef ブロック） ----
int  OsEnsureReady();                                   // 0 / 3（ウィンドウ作成失敗）
int  OsRegister(const std::string& canonical, bool alt, bool ctrl, bool shift, int os_id); // 0 / 5
int  OsUnregister(int os_id);                            // 0 / 5
bool OsForegroundIsHost();                               // 自プロセスが前面か
void OsCleanup();                                        // ウィンドウ/ハンドラ破棄

// OS ハンドラ（メインスレッド）から呼ぶ: os_id の発火を必要なら積む。
void OnHotkeyFired(int os_id)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_id_to_key.find(os_id);
    if (it == g_id_to_key.end()) return;
    auto bit = g_registry.find(it->second);
    if (bit == g_registry.end()) return;
    const Binding& b = bit->second;
    if (!b.global && !OsForegroundIsHost()) return;   // 非グローバルは前面時のみ
    g_fire_queue.push_back(HotkeyFire{ b.file, b.script, b.param });
}

} // namespace

// ===========================================================================
// 純粋ロジック
// ===========================================================================

bool ParseHotkeyName(const std::string& name, std::string& canonical_out)
{
    std::string t = TrimUpper(name);
    if (t == "ESCAPE") t = "ESC";
    if (t == "SPACEBAR") t = "SPACE";
    if (!IsKnownToken(t)) return false;
    canonical_out = t;
    return true;
}

std::string HotkeySignature(const std::string& canonical_key, bool alt, bool ctrl, bool shift)
{
    std::string s;
    if (ctrl)  s += "CTRL+";
    if (alt)   s += "ALT+";
    if (shift) s += "SHIFT+";
    s += canonical_key;
    return s;
}

// ===========================================================================
// 登録 API
// ===========================================================================

int HotkeyAdd(const std::string& hotkey, const std::string& file, const std::string& script,
              const std::string& param, bool alt, bool ctrl, bool shift, bool global)
{
    std::string canonical;
    if (!ParseHotkeyName(hotkey, canonical)) return 2;   // 不明キー

    std::lock_guard<std::mutex> lk(g_mutex);

    if (g_registry.find(canonical) != g_registry.end()) return 4;  // 既登録

    const int ready = OsEnsureReady();
    if (ready) return ready;   // 3（ウィンドウ作成失敗）

    const int os_id = g_next_id;
    const int reg = OsRegister(canonical, alt, ctrl, shift, os_id);
    if (reg) return reg;       // 5（登録失敗）

    ++g_next_id;
    Binding b;
    b.canonical = canonical;
    b.display   = HotkeySignature(canonical, alt, ctrl, shift);
    b.file = file; b.script = script; b.param = param;
    b.alt = alt; b.ctrl = ctrl; b.shift = shift; b.global = global;
    b.os_id = os_id;
    g_registry[canonical] = b;
    g_id_to_key[os_id] = canonical;
    return 0;
}

int HotkeyList(const std::string& separator, std::string& list_out)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_registry.empty()) return 2;   // 未登録
    std::string out;
    bool first = true;
    for (const auto& kv : g_registry) {
        if (!first) out += separator;
        out += kv.second.display;
        first = false;
    }
    list_out = out;
    return 0;
}

int HotkeyRemove(const std::string& hotkey)
{
    std::string canonical;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (g_registry.empty()) return 2;   // 未登録（0 件）
    }
    if (!ParseHotkeyName(hotkey, canonical)) return 3;  // 不明キー

    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_registry.find(canonical);
    if (it == g_registry.end()) return 4;   // 該当なし

    const int os_id = it->second.os_id;
    const int unreg = OsUnregister(os_id);
    if (unreg) return unreg;   // 5（解除失敗）

    g_registry.erase(it);
    g_id_to_key.erase(os_id);
    return 0;
}

void HotkeyDrainPending(std::vector<HotkeyFire>& out)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    out.assign(g_fire_queue.begin(), g_fire_queue.end());
    g_fire_queue.clear();
}

void HotkeyShutdown()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    for (const auto& kv : g_registry) OsUnregister(kv.second.os_id);
    g_registry.clear();
    g_id_to_key.clear();
    g_fire_queue.clear();
    OsCleanup();
}

// ===========================================================================
// プラットフォーム層の実装
// ===========================================================================

namespace {

#if defined(_WIN32)

HWND g_msg_window = nullptr;

UINT TokenToVk(const std::string& t)
{
    if (t.size() == 1) {
        const char c = t[0];
        if (c >= 'A' && c <= 'Z') return static_cast<UINT>(c);          // 'A'..'Z' = VK
        if (c >= '0' && c <= '9') return static_cast<UINT>(c);          // '0'..'9' = VK
    }
    if (t.size() >= 2 && t[0] == 'F') return VK_F1 + (std::atoi(t.c_str() + 1) - 1);
    if (t == "SPACE") return VK_SPACE;
    if (t == "ESC")   return VK_ESCAPE;
    if (t == "END")   return VK_END;
    if (t == "HOME")  return VK_HOME;
    if (t == "UP")    return VK_UP;
    if (t == "DOWN")  return VK_DOWN;
    if (t == "LEFT")  return VK_LEFT;
    if (t == "RIGHT") return VK_RIGHT;
    return 0;
}

LRESULT CALLBACK HotkeyWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_HOTKEY) {
        OnHotkeyFired(static_cast<int>(wp));
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int OsEnsureReady()
{
    if (g_msg_window) return 0;
    static const wchar_t* kClass = L"ZooPlugHotkeyWindow";
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HotkeyWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);   // 二重登録は無害（既存なら失敗するだけ）
    g_msg_window = CreateWindowExW(0, kClass, L"ZooPlugHotkey", 0, 0, 0, 0, 0,
                                   HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    return g_msg_window ? 0 : 3;
}

int OsRegister(const std::string& canonical, bool alt, bool ctrl, bool shift, int os_id)
{
    const UINT vk = TokenToVk(canonical);
    if (vk == 0) return 5;
    UINT mods = (alt ? MOD_ALT : 0) | (ctrl ? MOD_CONTROL : 0) | (shift ? MOD_SHIFT : 0);
#ifdef MOD_NOREPEAT
    mods |= MOD_NOREPEAT;   // オートリピートで連射させない（Win7+）
#endif
    return RegisterHotKey(g_msg_window, os_id, mods, vk) ? 0 : 5;
}

int OsUnregister(int os_id)
{
    return UnregisterHotKey(g_msg_window, os_id) ? 0 : 5;
}

bool OsForegroundIsHost()
{
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

void OsCleanup()
{
    if (g_msg_window) { DestroyWindow(g_msg_window); g_msg_window = nullptr; }
}

#elif defined(__APPLE__)

bool g_handler_installed = false;
std::map<int, EventHotKeyRef> g_mac_refs;

// 正規キー名 → Carbon 仮想キーコード（kVK_*）。0xFFFF = 不明。
UInt32 TokenToVk(const std::string& t)
{
    if (t.size() == 1) {
        switch (t[0]) {
            case 'A': return kVK_ANSI_A; case 'B': return kVK_ANSI_B; case 'C': return kVK_ANSI_C;
            case 'D': return kVK_ANSI_D; case 'E': return kVK_ANSI_E; case 'F': return kVK_ANSI_F;
            case 'G': return kVK_ANSI_G; case 'H': return kVK_ANSI_H; case 'I': return kVK_ANSI_I;
            case 'J': return kVK_ANSI_J; case 'K': return kVK_ANSI_K; case 'L': return kVK_ANSI_L;
            case 'M': return kVK_ANSI_M; case 'N': return kVK_ANSI_N; case 'O': return kVK_ANSI_O;
            case 'P': return kVK_ANSI_P; case 'Q': return kVK_ANSI_Q; case 'R': return kVK_ANSI_R;
            case 'S': return kVK_ANSI_S; case 'T': return kVK_ANSI_T; case 'U': return kVK_ANSI_U;
            case 'V': return kVK_ANSI_V; case 'W': return kVK_ANSI_W; case 'X': return kVK_ANSI_X;
            case 'Y': return kVK_ANSI_Y; case 'Z': return kVK_ANSI_Z;
            case '0': return kVK_ANSI_0; case '1': return kVK_ANSI_1; case '2': return kVK_ANSI_2;
            case '3': return kVK_ANSI_3; case '4': return kVK_ANSI_4; case '5': return kVK_ANSI_5;
            case '6': return kVK_ANSI_6; case '7': return kVK_ANSI_7; case '8': return kVK_ANSI_8;
            case '9': return kVK_ANSI_9;
        }
    }
    if (t.size() >= 2 && t[0] == 'F') {
        switch (std::atoi(t.c_str() + 1)) {
            case 1: return kVK_F1;  case 2: return kVK_F2;  case 3: return kVK_F3;
            case 4: return kVK_F4;  case 5: return kVK_F5;  case 6: return kVK_F6;
            case 7: return kVK_F7;  case 8: return kVK_F8;  case 9: return kVK_F9;
            case 10: return kVK_F10; case 11: return kVK_F11; case 12: return kVK_F12;
        }
    }
    if (t == "SPACE") return kVK_Space;
    if (t == "ESC")   return kVK_Escape;
    if (t == "END")   return kVK_End;
    if (t == "HOME")  return kVK_Home;
    if (t == "UP")    return kVK_UpArrow;
    if (t == "DOWN")  return kVK_DownArrow;
    if (t == "LEFT")  return kVK_LeftArrow;
    if (t == "RIGHT") return kVK_RightArrow;
    return 0xFFFF;
}

OSStatus HotKeyHandler(EventHandlerCallRef /*ref*/, EventRef event, void* /*ud*/)
{
    EventHotKeyID hk;
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                          sizeof(hk), nullptr, &hk) == noErr) {
        OnHotkeyFired(static_cast<int>(hk.id));
    }
    return noErr;
}

int OsEnsureReady()
{
    if (g_handler_installed) return 0;
    EventTypeSpec spec = { kEventClassKeyboard, kEventHotKeyPressed };
    if (InstallApplicationEventHandler(&HotKeyHandler, 1, &spec, nullptr, nullptr) != noErr) {
        return 3;
    }
    g_handler_installed = true;
    return 0;
}

int OsRegister(const std::string& canonical, bool alt, bool ctrl, bool shift, int os_id)
{
    const UInt32 vk = TokenToVk(canonical);
    if (vk == 0xFFFF) return 5;
    UInt32 mods = (alt ? optionKey : 0) | (ctrl ? controlKey : 0) | (shift ? shiftKey : 0);
    EventHotKeyID hk; hk.signature = 'ZOOP'; hk.id = static_cast<UInt32>(os_id);
    EventHotKeyRef ref = nullptr;
    if (RegisterEventHotKey(vk, mods, hk, GetApplicationEventTarget(), 0, &ref) != noErr || !ref) {
        return 5;
    }
    g_mac_refs[os_id] = ref;
    return 0;
}

int OsUnregister(int os_id)
{
    auto it = g_mac_refs.find(os_id);
    if (it == g_mac_refs.end()) return 5;
    const OSStatus st = UnregisterEventHotKey(it->second);
    g_mac_refs.erase(it);
    return st == noErr ? 0 : 5;
}

// macOS の RegisterEventHotKey はシステム全体で発火する。前面判定には AppKit
// (Objective-C) が要りこのファイルを純 C++ に保つため、ここでは常に true（= 常にグローバル）。
// bGlobal=false の厳密な前面限定は TODO-compat（必要なら .mm 化して NSWorkspace を使う）。
bool OsForegroundIsHost() { return true; }

void OsCleanup()
{
    for (auto& kv : g_mac_refs) UnregisterEventHotKey(kv.second);
    g_mac_refs.clear();
    // インストール済みハンドラはプロセス終了で解放される（個別 Remove は不要）。
}

#else // Linux 等: グローバルホットキー非対応

int  OsEnsureReady() { return 3; }   // ホットキーウィンドウ作成不可
int  OsRegister(const std::string&, bool, bool, bool, int) { return 5; }
int  OsUnregister(int) { return 5; }
bool OsForegroundIsHost() { return false; }
void OsCleanup() {}

#endif

} // namespace

} // namespace zoo

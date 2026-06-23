// test_hotkeyops.cpp
//
// HotkeyOps の純粋ロジック（キー名パース・シグネチャ）と、OS 登録に触れない範囲の
// 振る舞い（空レジストリでの List/Remove）を FileMaker 無しで検証する。
// 実際のホットキー登録（RegisterHotKey / RegisterEventHotKey）はシステム全体に影響するので
// 単体テストでは呼ばない（実機確認は FileMaker Pro（実機））。
//
//   macOS : c++ -std=c++17 -I../Source ../Source/HotkeyOps.cpp test_hotkeyops.cpp -framework Carbon -o t && ./t
//
// Part of ZooPlug. License: see License.txt

#include "HotkeyOps.h"

#include <cstdio>
#include <string>

namespace {
int g_failures = 0;
void check_true(bool cond, const char* label) {
    if (cond) std::printf("  [PASS] %s\n", label);
    else { std::printf("  [FAIL] %s\n", label); ++g_failures; }
}
void check_eq(const std::string& a, const std::string& b, const char* label) {
    if (a == b) std::printf("  [PASS] %s\n", label);
    else { std::printf("  [FAIL] %s expected=[%s] actual=[%s]\n", label, b.c_str(), a.c_str()); ++g_failures; }
}
void check_eq_int(int a, int b, const char* label) {
    if (a == b) std::printf("  [PASS] %s\n", label);
    else { std::printf("  [FAIL] %s expected=%d actual=%d\n", label, b, a); ++g_failures; }
}
} // namespace

int main() {
    using namespace zoo;

    std::printf("ParseHotkeyName:\n");
    std::string c;
    check_true(ParseHotkeyName("a", c) && c == "A", "letter lowercased -> A");
    check_true(ParseHotkeyName(" F12 ", c) && c == "F12", "trim + F12");
    check_true(ParseHotkeyName("f1", c) && c == "F1", "f1 -> F1");
    check_true(ParseHotkeyName("escape", c) && c == "ESC", "escape alias -> ESC");
    check_true(ParseHotkeyName("Esc", c) && c == "ESC", "Esc -> ESC");
    check_true(ParseHotkeyName("space", c) && c == "SPACE", "space -> SPACE");
    check_true(ParseHotkeyName("Up", c) && c == "UP", "Up -> UP");
    check_true(ParseHotkeyName("7", c) && c == "7", "digit 7");
    check_true(!ParseHotkeyName("F13", c), "F13 rejected (only F1-F12)");
    check_true(!ParseHotkeyName("F0", c), "F0 rejected");
    check_true(!ParseHotkeyName("AB", c), "multi-letter rejected");
    check_true(!ParseHotkeyName("", c), "empty rejected");
    check_true(!ParseHotkeyName("Tab", c), "unsupported key (Tab) rejected");

    std::printf("HotkeySignature:\n");
    check_eq(HotkeySignature("A", false, true, false), "CTRL+A", "ctrl only");
    check_eq(HotkeySignature("A", true, true, true), "CTRL+ALT+SHIFT+A", "all modifiers ordered");
    check_eq(HotkeySignature("F1", false, false, false), "F1", "no modifiers");
    check_eq(HotkeySignature("UP", true, false, false), "ALT+UP", "alt only");

    std::printf("empty-registry behavior (no OS calls):\n");
    std::string list;
    check_eq_int(HotkeyList("|", list), 2, "List on empty -> Err_2");
    check_eq_int(HotkeyRemove("A"), 2, "Remove on empty -> Err_2 (empty checked first)");
    check_eq_int(HotkeyRemove("garbage"), 2, "Remove(garbage) on empty -> Err_2");
    std::vector<HotkeyFire> fires;
    HotkeyDrainPending(fires);
    check_true(fires.empty(), "drain on empty queue -> none");

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

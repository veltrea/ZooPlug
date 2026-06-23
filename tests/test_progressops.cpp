// test_progressops.cpp
//
// Moo_ProgressOptions の状態保持を FileMaker 無しで検証する。
//   macOS/Linux: c++ -std=c++17 -I../Source ../Source/ProgressOps.cpp test_progressops.cpp -o t && ./t
//
// Part of ZooPlug. License: see License.txt

#include "ProgressOps.h"

#include <cstdio>

namespace {
int g_failures = 0;
void check_true(bool cond, const char* label) {
    if (cond) std::printf("  [PASS] %s\n", label);
    else { std::printf("  [FAIL] %s\n", label); ++g_failures; }
}
} // namespace

int main() {
    using namespace zoo;

    std::printf("ProgressOptions:\n");
    ClearProgressOptions();
    check_true(!GetProgressOptions().set, "starts unset");

    check_true(SetProgressOptions(u8"題名 表予能", u8"説明", true) == 0, "set returns 0");
    ProgressOptions p = GetProgressOptions();
    check_true(p.set, "set flag true after set");
    check_true(p.title == u8"題名 表予能", "title stored (UTF-8 preserved)");
    check_true(p.caption == u8"説明", "caption stored");
    check_true(p.cancel == true, "cancel stored");

    // 上書き
    check_true(SetProgressOptions("t2", "", false) == 0, "second set returns 0");
    p = GetProgressOptions();
    check_true(p.title == "t2" && p.caption.empty() && p.cancel == false, "overwrite works");

    ClearProgressOptions();
    check_true(!GetProgressOptions().set, "cleared");

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

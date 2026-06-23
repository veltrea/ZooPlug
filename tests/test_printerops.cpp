// test_printerops.cpp
//
// PrinterOps の取得/一覧を FileMaker 無しで検証する。既定プリンタを変更する破壊的な
// PrinterSetDefault は呼ばない（rc 規約だけ read 系から確認）。プリンタの有無に依存せず
// 通る作りにする（無ければ Get=2 / List=3 が正しい挙動）。
//
//   macOS/Linux: c++ -std=c++17 -I../Source ../Source/PrinterOps.cpp test_printerops.cpp -lcups -o t && ./t
//
// Part of ZooPlug. License: see License.txt

#include "PrinterOps.h"

#include <cstdio>
#include <string>

namespace {
int g_failures = 0;
void check_true(bool cond, const char* label) {
    if (cond) std::printf("  [PASS] %s\n", label);
    else { std::printf("  [FAIL] %s\n", label); ++g_failures; }
}
bool contains(const std::string& hay, const std::string& needle) {
    return !needle.empty() && hay.find(needle) != std::string::npos;
}
} // namespace

int main() {
    using namespace zoo;

    std::printf("PrinterList:\n");
    std::string list;
    const int lrc = PrinterList("|", list);
    std::printf("       rc=%d list=[%s]\n", lrc, list.c_str());
    check_true(lrc == 0 || lrc == 2 || lrc == 3, "PrinterList rc in {0,2,3}");
    if (lrc == 0) check_true(!list.empty(), "rc=0 implies non-empty list");

    std::printf("PrinterGetDefault:\n");
    std::string def;
    const int grc = PrinterGetDefault(def);
    std::printf("       rc=%d default=[%s]\n", grc, def.c_str());
    check_true(grc == 0 || grc == 2, "PrinterGetDefault rc in {0,2}");
    if (grc == 0) check_true(!def.empty(), "rc=0 implies non-empty name");

    // 不変条件: 既定が取得でき、一覧も取得できたなら、既定は一覧に含まれるはず
    if (grc == 0 && lrc == 0) {
        check_true(contains(list, def), "default printer appears in the list");
    }

    // 存在しないプリンタの設定は失敗（3）。これは「設定しようとして失敗」なので
    // 既定は変わらない（破壊的でない）。
    std::printf("PrinterSetDefault (non-existent -> Err_3, non-destructive):\n");
    check_true(PrinterSetDefault("zoo_no_such_printer_xyz_123") == 3,
               "setting a non-existent printer returns 3");

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

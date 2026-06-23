// test_processops.cpp
//
// ProcessOps の純粋ロジック（名前マッチ・件数・一覧・エラー番号）と、実プロセス列挙を
// FileMaker 無しで検証するスタンドアロンテスト。実プロセスを終了させる破壊的テストはしない
// （ProcessKill は空入力=2 と「一致なし=2」だけ確認する）。
//
//   macOS/Linux: c++ -std=c++17 -I../Source ../Source/ProcessOps.cpp test_processops.cpp -o t && ./t
//   （macOS は libproc がシステムにあるので追加リンク不要）
//
// Part of ZooPlug. License: see License.txt

#include "ProcessOps.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#  include <windows.h>
#  define ZOO_GETPID() static_cast<unsigned long>(GetCurrentProcessId())
#else
#  include <unistd.h>
#  define ZOO_GETPID() static_cast<unsigned long>(getpid())
#endif

namespace {

int g_failures = 0;

void check_true(bool cond, const char* label) {
    if (cond) std::printf("  [PASS] %s\n", label);
    else { std::printf("  [FAIL] %s\n", label); ++g_failures; }
}

void check_eq_int(long actual, long expected, const char* label) {
    if (actual == expected) std::printf("  [PASS] %s\n", label);
    else { std::printf("  [FAIL] %s  expected=%ld actual=%ld\n", label, expected, actual); ++g_failures; }
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

} // namespace

int main() {
    using namespace zoo;

    // ---- ProcessNameMatches（純粋ロジック）----
    std::printf("ProcessNameMatches:\n");
    check_true(ProcessNameMatches("notepad.exe", "notepad"), ".exe absorbed (proc has, query not)");
    check_true(ProcessNameMatches("notepad", "notepad.exe"), ".exe absorbed (query has, proc not)");
    check_true(ProcessNameMatches("Finder", "finder"), "case-insensitive");
    check_true(ProcessNameMatches("Chrome.EXE", "chrome"), "case + .exe");
    check_true(!ProcessNameMatches("chrome", "firefox"), "different names do not match");
    check_true(!ProcessNameMatches("", "x"), "empty proc name no match");

    // ---- 列挙 ----
    std::printf("EnumerateProcesses:\n");
    const auto procs = EnumerateProcesses();
    check_true(!procs.empty(), "enumerates at least one process");

    // 自プロセスは必ず読めるので、それを基準にする（権限で名前が取れない他人の
    // プロセスはスキップされ得るため、外部の既知プロセス名には頼らない）。
    const unsigned long my_pid = ZOO_GETPID();
    std::string my_name;
    for (const auto& p : procs) if (p.pid == my_pid) { my_name = p.name; break; }
    std::printf("       self pid=%lu name=[%s]\n", my_pid, my_name.c_str());
    check_true(!my_name.empty(), "self process is in the enumeration");

    // ---- ProcessCount ----
    std::printf("ProcessCount:\n");
    long all = -1;
    check_eq_int(ProcessCount("", all), 0, "rc=0 for empty (count all)");
    check_eq_int(all, static_cast<long>(procs.size()), "empty -> total process count");
    if (!my_name.empty()) {
        long mine = -1;
        ProcessCount(my_name, mine);
        check_true(mine >= 1, "self process name counted >= 1");
    }

    // ---- ProcessRunning ----
    std::printf("ProcessRunning:\n");
    bool r = true;
    check_eq_int(ProcessRunning("", r), 2, "empty input -> Err_2");
    if (!my_name.empty()) {
        bool rk = false;
        check_eq_int(ProcessRunning(my_name, rk), 0, "rc=0 for self process");
        check_true(rk, "self process is running");
    }
    bool rn = true;
    ProcessRunning("zoo_definitely_no_such_proc_xyz", rn);
    check_true(!rn, "nonexistent process not running");

    // ---- ProcessList ----
    std::printf("ProcessList:\n");
    std::string list;
    check_eq_int(ProcessList("|", list), 0, "rc=0");
    check_true(!list.empty(), "list non-empty");
    if (procs.size() >= 2) check_true(contains(list, "|"), "separator present with 2+ procs");

    // ---- ProcessKill（破壊的操作はしない）----
    std::printf("ProcessKill (non-destructive checks):\n");
    check_eq_int(ProcessKill(""), 2, "empty input -> Err_2");
    check_eq_int(ProcessKill("zoo_definitely_no_such_proc_xyz"), 2, "no match -> Err_2");

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}

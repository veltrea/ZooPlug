// ProcessOps.cpp
//
// Process 4 関数の実装。詳細は ProcessOps.h を参照。
//   Windows : Toolhelp32（列挙）+ OpenProcess/TerminateProcess（終了）
//   macOS   : libproc（列挙）+ kill(2)（終了）
//   Linux   : /proc（列挙）+ kill(2)（終了）
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

#include "ProcessOps.h"

#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#  include <tlhelp32.h>
#else
#  include <csignal>
#  ifdef __APPLE__
#    include <libproc.h>
#  else
#    include <cstdio>
#    include <dirent.h>
#  endif
#endif

namespace zoo {

namespace {

std::string ToLowerAscii(std::string s)
{
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// 末尾の ".exe"（大小無視）を除いた小文字名にする（マッチ正規化用）。
std::string NormalizeProcName(const std::string& name)
{
    std::string s = ToLowerAscii(name);
    const std::string ext = ".exe";
    if (s.size() > ext.size() && s.compare(s.size() - ext.size(), ext.size(), ext) == 0) {
        s.erase(s.size() - ext.size());
    }
    return s;
}

#ifdef _WIN32
std::string WideToUtf8(const wchar_t* w)
{
    if (!w || !*w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(n - 1, '\0'); // n には終端 null を含む
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
#endif

// pid を強制終了する。成功で true。
bool TerminatePid(unsigned long pid)
{
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok != 0;
#else
    return ::kill(static_cast<pid_t>(pid), SIGKILL) == 0;
#endif
}

} // namespace

std::vector<ProcInfo> EnumerateProcesses()
{
    std::vector<ProcInfo> out;

#ifdef _WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            ProcInfo pi;
            pi.pid  = pe.th32ProcessID;
            pi.name = WideToUtf8(pe.szExeFile);
            if (!pi.name.empty()) out.push_back(pi);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

#elif defined(__APPLE__)
    int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (bytes <= 0) return out;
    std::vector<pid_t> pids(static_cast<std::size_t>(bytes) / sizeof(pid_t) + 16, 0);
    bytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(),
                          static_cast<int>(pids.size() * sizeof(pid_t)));
    if (bytes <= 0) return out;
    const int got = bytes / static_cast<int>(sizeof(pid_t));
    for (int i = 0; i < got; ++i) {
        const pid_t pid = pids[i];
        if (pid <= 0) continue;
        char name[2 * MAXCOMLEN + 1] = {0};
        if (proc_name(pid, name, sizeof(name)) > 0) {
            ProcInfo pi;
            pi.pid  = static_cast<unsigned long>(pid);
            pi.name = name;
            out.push_back(pi);
        }
    }

#else // Linux: /proc/<pid>/comm
    DIR* d = opendir("/proc");
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        // 数字だけのエントリが pid
        const char* p = e->d_name;
        bool digits = *p != '\0';
        for (const char* q = p; *q; ++q) if (*q < '0' || *q > '9') { digits = false; break; }
        if (!digits) continue;
        std::string comm = std::string("/proc/") + p + "/comm";
        if (FILE* f = std::fopen(comm.c_str(), "rb")) {
            char buf[256] = {0};
            std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
            std::fclose(f);
            while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
            if (n > 0) {
                ProcInfo pi;
                pi.pid  = std::strtoul(p, nullptr, 10);
                pi.name = buf;
                out.push_back(pi);
            }
        }
    }
    closedir(d);
#endif

    return out;
}

bool ProcessNameMatches(const std::string& proc_name, const std::string& query)
{
    return NormalizeProcName(proc_name) == NormalizeProcName(query);
}

int ProcessCount(const std::string& name_utf8, long& count_out)
{
    const std::vector<ProcInfo> procs = EnumerateProcesses();
    if (name_utf8.empty()) {
        count_out = static_cast<long>(procs.size());
        return 0;
    }
    long n = 0;
    for (const ProcInfo& p : procs) if (ProcessNameMatches(p.name, name_utf8)) ++n;
    count_out = n;
    return 0;
}

int ProcessRunning(const std::string& name_utf8, bool& running_out)
{
    if (name_utf8.empty()) return 2;
    const std::vector<ProcInfo> procs = EnumerateProcesses();
    for (const ProcInfo& p : procs) {
        if (ProcessNameMatches(p.name, name_utf8)) { running_out = true; return 0; }
    }
    running_out = false;
    return 0;
}

int ProcessList(const std::string& separator_utf8, std::string& list_out)
{
    const std::vector<ProcInfo> procs = EnumerateProcesses();
    std::string out;
    bool first = true;
    for (const ProcInfo& p : procs) {
        if (!first) out += separator_utf8;
        out += p.name;
        first = false;
    }
    list_out = out;
    return 0;
}

int ProcessKill(const std::string& name_utf8)
{
    if (name_utf8.empty()) return 2;
    const std::vector<ProcInfo> procs = EnumerateProcesses();
    bool any = false, all_ok = true;
    for (const ProcInfo& p : procs) {
        if (ProcessNameMatches(p.name, name_utf8)) {
            any = true;
            if (!TerminatePid(p.pid)) all_ok = false;
        }
    }
    if (!any || !all_ok) return 2;   // 一致無し or 終了失敗
    return 0;
}

} // namespace zoo

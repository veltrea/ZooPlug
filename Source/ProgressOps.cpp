// ProgressOps.cpp
//
// Moo_ProgressOptions の状態保持。詳細は ProgressOps.h を参照。
//
// Part of ZooPlug. License: see License.txt

#include "ProgressOps.h"

#include <mutex>

namespace zoo {

namespace {
std::mutex g_progress_mutex;
ProgressOptions g_progress;
} // namespace

int SetProgressOptions(const std::string& title, const std::string& caption, bool cancel)
{
    std::lock_guard<std::mutex> lk(g_progress_mutex);
    g_progress.set     = true;
    g_progress.title   = title;
    g_progress.caption = caption;
    g_progress.cancel  = cancel;
    return 0;
}

ProgressOptions GetProgressOptions()
{
    std::lock_guard<std::mutex> lk(g_progress_mutex);
    return g_progress;
}

void ClearProgressOptions()
{
    std::lock_guard<std::mutex> lk(g_progress_mutex);
    g_progress = ProgressOptions{};
}

} // namespace zoo

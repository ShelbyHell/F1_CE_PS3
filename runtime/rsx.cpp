// Picks the renderer: Vulkan native backend for RSX FIFO.
#include "rsx.h"
#include "kernel.h"

#include <cstdlib>
#include <deque>
#include <cstring>

std::atomic<bool> g_rsx_starting{false};

namespace rsx {
bool g_plain_on = false;
bool g_plain[0x800];
void (*g_upload_words)(u32, const u32*, u32, bool) = nullptr;
FrontProf g_fe;

ZpassMode zpass_mode() {
    static const ZpassMode m = [] {
        const char* e = getenv("F1CE_ZPASS_PRECISE");
        if (e && !strcmp(e, "flip")) return ZpassMode::flip;
        if (e && !strcmp(e, "0")) return ZpassMode::late;
        return ZpassMode::lazy;
    }();
    return m;
}
std::atomic<u64> g_frames_done{0};
std::atomic<u64> g_report_oldest{~0ull};
std::atomic<u64> g_report_want{0};
static std::deque<u64> s_report_frames;   // frames of the pending reports, oldest first (RSX thread only)
void report_issued() {
    const u64 f = g_frames_done.load(std::memory_order_relaxed);
    if (s_report_frames.empty()) g_report_oldest.store(f, std::memory_order_release);
    s_report_frames.push_back(f);
}
void report_written() {
    if (s_report_frames.empty()) return;
    s_report_frames.pop_front();
    g_report_oldest.store(s_report_frames.empty() ? ~0ull : s_report_frames.front(), std::memory_order_release);
}

namespace vk {
void init();
void method(u32, u32);
void flip(u32, u32, u32, u32);
bool report(u32, u32);
std::string mem_report();
void zpass_service();
}

bool use_vulkan() { return true; }

void init() { vk::init(); }
void method(u32 m, u32 arg) { vk::method(m, arg); }
void flip(u32 ea, u32 pitch, u32 w, u32 h) { vk::flip(ea, pitch, w, h); }
bool report(u32 type, u32 ea) { return vk::report(type, ea); }
std::string mem_report() { return vk::mem_report(); }
void zpass_service() { vk::zpass_service(); }

}  // namespace rsx

// Picks the renderer: OpenGL unless F1CE_RENDERER=vulkan (or vk). Decided once at start-up, like the window type.
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

namespace gl { void init(); void method(u32, u32); void flip(u32, u32, u32, u32); bool report(u32, u32); std::string mem_report(); void zpass_service(); }
#ifdef F1CE_VULKAN
namespace vk { void init(); void method(u32, u32); void flip(u32, u32, u32, u32); bool report(u32, u32); std::string mem_report(); void zpass_service(); }

bool use_vulkan() {
    static const bool v = [] {
        const char* e = getenv("F1CE_RENDERER");
        return e && (!strcmp(e, "vulkan") || !strcmp(e, "vk"));
    }();
    return v;
}

void init() { if (use_vulkan()) vk::init(); else gl::init(); }
void method(u32 m, u32 arg) { if (use_vulkan()) vk::method(m, arg); else gl::method(m, arg); }
void flip(u32 ea, u32 pitch, u32 w, u32 h) { if (use_vulkan()) vk::flip(ea, pitch, w, h); else gl::flip(ea, pitch, w, h); }
bool report(u32 type, u32 ea) { return use_vulkan() ? vk::report(type, ea) : gl::report(type, ea); }
std::string mem_report() { return use_vulkan() ? vk::mem_report() : gl::mem_report(); }
void zpass_service() { if (use_vulkan()) vk::zpass_service(); else gl::zpass_service(); }
#else
bool use_vulkan() {
    static const bool asked = [] {
        const char* e = getenv("F1CE_RENDERER");
        return e && (!strcmp(e, "vulkan") || !strcmp(e, "vk"));
    }();
    if (asked) { static bool once = (LOG_W("F1CE_RENDERER=vulkan: built without F1CE_VULKAN, using OpenGL"), true); (void)once; }
    return false;
}

void init() { use_vulkan(); gl::init(); }
void method(u32 m, u32 arg) { gl::method(m, arg); }
void flip(u32 ea, u32 pitch, u32 w, u32 h) { gl::flip(ea, pitch, w, h); }
bool report(u32 type, u32 ea) { return gl::report(type, ea); }
std::string mem_report() { return gl::mem_report(); }
void zpass_service() { gl::zpass_service(); }
#endif
}  // namespace rsx

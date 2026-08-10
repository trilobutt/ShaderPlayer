#include "FrameProfiler.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace SP::Profile {

namespace {

struct Accum {
    double total = 0.0;
    double worst = 0.0;
    unsigned long long calls = 0;
};

// GUI-thread only: no locking, no growth. Fixed at kSectionCount entries plus the
// three input-latency scalars, zeroed on every 5-second flush.
Accum g_sections[kSectionCount];
Accum g_input;

unsigned long long g_frameCount = 0;
double g_lastFlushMs = 0.0;   // 0.0 is the "not yet established" sentinel

const char* SectionName(Section s) {
    switch (s) {
    case kTick:            return "Tick";
    case kProcessFrame:    return "ProcessFrame";
    case kCheckForChanges: return "CheckForChanges";
    case kVideoUpload:     return "VideoUpload";
    case kRender:          return "Render";
    case kMainWindowTick:  return "MainWindowTick";
    case kViewportPresent: return "Present";
    case kEventLoopGap:    return "EventLoopGap";
    default:               return "Unknown";
    }
}

void WriteBlock(unsigned long long frames, double elapsedSec) {
    FILE* f = nullptr;
    if (fopen_s(&f, "profile.log", "a") != 0 || !f) return;

    fprintf(f, "=== ShaderPlayer frame profile: %llu frames over %.2f s ===\n",
            frames, elapsedSec);
    fprintf(f, "%-32s%6s%11s%11s\n", "name", "avg_ms", "worst_ms", "calls");

    for (int i = 0; i < kSectionCount; ++i) {
        const Accum& a = g_sections[i];
        if (a.calls == 0) continue;
        const double avg = a.total / static_cast<double>(a.calls);
        fprintf(f, "%-32s%6.3f%11.3f%11llu\n",
                SectionName(static_cast<Section>(i)), avg, a.worst, a.calls);
    }

    if (g_input.calls != 0) {
        const double avg = g_input.total / static_cast<double>(g_input.calls);
        fprintf(f, "%-32s%6.3f%11.3f%11llu\n",
                "InputLatency", avg, g_input.worst, g_input.calls);
    }

    fclose(f);
}

}  // namespace

bool Enabled() {
    static const bool enabled = [] {
        char* value = nullptr;
        size_t len = 0;
        if (_dupenv_s(&value, &len, "SHADERPLAYER_PROFILE") != 0 || !value) return false;
        const bool on = (value[0] == '1' && value[1] == '\0');
        free(value);
        return on;
    }();
    return enabled;
}

double Now() {
    static const LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    static const LARGE_INTEGER epoch = [] {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return c;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - epoch.QuadPart) * 1000.0
         / static_cast<double>(freq.QuadPart);
}

void Add(Section s, double ms) {
    if (!Enabled()) return;
    Accum& a = g_sections[s];
    a.total += ms;
    if (ms > a.worst) a.worst = ms;
    ++a.calls;
}

void NoteInput(double ms) {
    if (!Enabled()) return;
    g_input.total += ms;
    if (ms > g_input.worst) g_input.worst = ms;
    ++g_input.calls;
}

void EndFrame() {
    if (!Enabled()) return;

    ++g_frameCount;
    const double now = Now();

    if (g_lastFlushMs == 0.0) {
        g_lastFlushMs = now;
        return;
    }

    const double elapsedSec = (now - g_lastFlushMs) / 1000.0;
    if (elapsedSec < 5.0) return;

    WriteBlock(g_frameCount, elapsedSec);

    for (auto& a : g_sections) a = Accum{};
    g_input = Accum{};
    g_frameCount = 0;
    g_lastFlushMs = now;
}

Scope::Scope(Section s) : m_section(s), m_start(0.0) {
    if (Enabled()) m_start = Now();
}

Scope::~Scope() {
    if (Enabled()) Add(m_section, Now() - m_start);
}

}  // namespace SP::Profile

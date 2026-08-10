#pragma once

namespace SP::Profile {

enum Section {
    kTick, kProcessFrame, kCheckForChanges, kVideoUpload, kRender,
    kMainWindowTick, kViewportPresent, kEventLoopGap, kSectionCount
};

// True when SHADERPLAYER_PROFILE=1 was in the environment at startup. Read once.
bool Enabled();

// Milliseconds from a QueryPerformanceCounter epoch. Public because main.cpp times
// the gap between ticks, which no scope can bracket.
double Now();

void Add(Section s, double ms);
void NoteInput(double ms);   // one input event's delivery latency
void EndFrame();             // once per tick; writes profile.log every 5 seconds

class Scope {
public:
    explicit Scope(Section s);
    ~Scope();
private:
    Section m_section;
    double  m_start;
};

}  // namespace SP::Profile

#define SP_PROFILE(section) SP::Profile::Scope _spProfileScope(SP::Profile::section)

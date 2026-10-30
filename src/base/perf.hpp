#pragma once
// Per-frame timings for the Info panel (KOMAL_PERF=1 also prints them every second), on the
// app's clock (base/clock.hpp).
//
//   { PerfScope s(Perf::Stroke); ... }      // adds the scope's time to this frame's counter
//   Perf::add(Perf::Acquire, ms);           // ... or a time measured elsewhere
//   Perf::endFrame();                       // once per frame

#include "base/clock.hpp"

namespace Perf {

enum Counter { Frame, Acquire, Stroke, CanvasPrepare, CanvasSync, Thumbnails, Graph, Count };   // Frame: CPU work, Acquire: waiting for the swapchain

void   add(Counter c, double ms);
void   endFrame();
double average(Counter c);   // ms per frame, smoothed
double worst(Counter c);     // ms, worst frame of the last second
const char* name(Counter c);
int    fps();                // frames rendered in the last second

}

class PerfScope {
public:
    explicit PerfScope(Perf::Counter c) : m_counter(c), m_start(Clock::now()) {}
    ~PerfScope() { Perf::add(m_counter, Clock::msSince(m_start)); }
    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    Perf::Counter m_counter;
    Clock::TimePoint m_start;
};

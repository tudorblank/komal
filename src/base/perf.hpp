#pragma once
// Per-frame timings for the Info panel (KOMAL_PERF=1 also prints them every second), on the
// app's clock (base/clock.hpp).
//
//   { PerfScope s(Perf::Stroke); ... }      // adds the scope's time to this frame's counter
//   Perf::add(Perf::Acquire, took);         // ... or a time measured elsewhere
//   Perf::endFrame();                       // once per frame

#include "base/clock.hpp"

namespace Perf {

enum Counter { Frame, Acquire, Stroke, CanvasPrepare, CanvasSync, Thumbnails, Graph, Count };   // Frame: CPU work, Acquire: waiting for the swapchain

void add(Counter c, Clock::Seconds took);
void endFrame();
Clock::Milliseconds average(Counter c);   // per frame, smoothed
Clock::Milliseconds worst(Counter c);     // the worst frame of the last second
const char* name(Counter c);
int    fps();                // frames rendered in the last second

}

class PerfScope {
public:
    explicit PerfScope(Perf::Counter c) : m_counter(c), m_start(Clock::now()) {}
    ~PerfScope() { Perf::add(m_counter, Clock::since(m_start)); }
    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    Perf::Counter m_counter;
    Clock::TimePoint m_start;
};

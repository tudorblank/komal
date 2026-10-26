#pragma once
// Per-frame CPU timings for the Info panel (KOMAL_PERF=1 also prints them every second).
//
//   { PerfScope s(Perf::Stroke); ... }      // adds the scope's time to this frame's counter
//   Perf::endFrame();                       // once per frame

#include <chrono>

namespace Perf {

enum Counter { Frame, Stroke, CanvasPrepare, CanvasSync, Thumbnails, Graph, Count };

void   add(Counter c, double ms);
void   endFrame();
double average(Counter c);   // ms per frame, smoothed
double worst(Counter c);     // ms, worst frame of the last second
const char* name(Counter c);

}

class PerfScope {
public:
    explicit PerfScope(Perf::Counter c) : m_counter(c), m_start(std::chrono::steady_clock::now()) {}
    ~PerfScope() { Perf::add(m_counter, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start).count()); }
    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

private:
    Perf::Counter m_counter;
    std::chrono::steady_clock::time_point m_start;
};

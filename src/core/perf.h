#pragma once
// Per-frame CPU timings of the main subsystems, for the Info panel (and KOMAL_PERF=1, which
// prints a summary to stderr every second: average and worst frame per counter).
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

} // namespace Perf

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

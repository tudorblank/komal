#pragma once
// The app's one clock: steady, never jumps. Everything that measures or waits on time reads it:
// Deadline (budgets), Perf (timings), the action log, frame pacing.
//
//   const Clock::TimePoint t = Clock::now();
//   ...
//   const double ms = Clock::msSince(t);

#include <chrono>

namespace Clock {

using Source    = std::chrono::steady_clock;
using TimePoint = Source::time_point;

inline TimePoint now() { return Source::now(); }
inline double msBetween(TimePoint from, TimePoint to) { return std::chrono::duration<double, std::milli>(to - from).count(); }
inline double msSince(TimePoint t) { return msBetween(t, now()); }
inline TimePoint after(TimePoint t, double ms)
{
    return t + std::chrono::duration_cast<Source::duration>(std::chrono::duration<double, std::milli>(ms));
}
double seconds();   // since the app started

}

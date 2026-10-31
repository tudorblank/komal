#pragma once
// The app's one clock: steady, never jumps. Everything that measures or waits on time reads it:
// Deadline (budgets), Perf (timings), the action log, frame pacing. Times are durations with their
// unit in the type (Seconds, Milliseconds: they convert to each other), never bare numbers.
//
//   const Clock::TimePoint t = Clock::now();
//   ...
//   const Clock::Milliseconds took = Clock::since(t);

#include <chrono>

namespace Clock {

using Source       = std::chrono::steady_clock;
using TimePoint    = Source::time_point;
using Seconds      = std::chrono::duration<double>;
using Milliseconds = std::chrono::duration<double, std::milli>;

inline TimePoint now() { return Source::now(); }
inline Seconds   between(TimePoint from, TimePoint to) { return to - from; }
inline Seconds   since(TimePoint t) { return now() - t; }
inline TimePoint after(TimePoint t, Seconds d) { return t + std::chrono::duration_cast<Source::duration>(d); }
Seconds sinceStart();   // since the app started

}

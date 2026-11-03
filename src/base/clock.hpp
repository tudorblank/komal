#pragma once

#include <chrono>

//  main app clock
//  const Clock::TimePoint t = Clock::now();
//  ...
//  const Clock::Milliseconds took = Clock::since(t);
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

//  time budget
//  const Deadline budget = Deadline::in(3ms);   // std::chrono_literals
//  for (...) { if (budget.expired()) break; ... }
class Deadline {
public:
    Deadline() = default;   // already expired

    static Deadline in(Clock::Seconds d) { return Deadline(Clock::after(Clock::now(), d)); }
    static Deadline earlier(const Deadline& a, const Deadline& b) { return a.m_end < b.m_end ? a : b; }
    bool expired() const { return Clock::now() >= m_end; }
    Clock::Seconds left() const { return std::max(Clock::Seconds(0), Clock::between(Clock::now(), m_end)); }

private:
    explicit Deadline(Clock::TimePoint end) : m_end(end) {}
    Clock::TimePoint m_end{};
};
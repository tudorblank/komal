#pragma once
// A time budget: work in small pieces checks expired() between them and stops when it's used up
// (what's left waits for the next frame). Also a timer: a hold that turns into something.
//
//   const Deadline budget = Deadline::in(3ms);   // std::chrono_literals
//   for (...) { if (budget.expired()) break; ... }

#include "base/clock.hpp"

#include <algorithm>

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

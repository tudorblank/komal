#pragma once
// A time budget: work in small pieces checks expired() between them and stops when it's used up
// (what's left waits for the next frame). Also a timer: a hold that turns into something.
//
//   const Deadline budget = Deadline::in(3.0);   // ms from now
//   for (...) { if (budget.expired()) break; ... }

#include "base/clock.hpp"

#include <algorithm>

class Deadline {
public:
    Deadline() = default;   // already expired

    static Deadline in(double ms) { return Deadline(Clock::after(Clock::now(), ms)); }
    static Deadline earlier(const Deadline& a, const Deadline& b) { return a.m_end < b.m_end ? a : b; }
    bool   expired() const { return Clock::now() >= m_end; }
    double msLeft() const { return std::max(0.0, Clock::msBetween(Clock::now(), m_end)); }

private:
    explicit Deadline(Clock::TimePoint end) : m_end(end) {}
    Clock::TimePoint m_end{};
};

#pragma once
// Frames are drawn on demand: when input arrives, and when something asks for one. Anything that
// changes by itself says when it next needs a frame, as time from the current frame's start
// (0: now; kNoFrame: only on input). Work finishing on another thread wakes the loop through the
// waker the main loop installs (Application, once at start).

#include "base/clock.hpp"

#include <limits>

constexpr Clock::Seconds kNoFrame{ std::numeric_limits<double>::infinity() };
constexpr Clock::Seconds kFrameNow{ 0.0 };

using MainLoopWaker = void (*)();
void installMainLoopWaker(MainLoopWaker waker);   // until one is installed, waking does nothing

// any thread: a frame soon, even if the loop is asleep.
void wakeMainLoop();

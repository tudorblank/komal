#pragma once
// When ImGui next changes what it draws by itself, without new input (base/frames.hpp): the
// main loop's say for ImGui, next to the UI's own (UserInterface::nextFrameIn). From public
// state only: now while input waits for ImGui, ticking while a tooltip may still appear or the
// caret blinks, at the key repeat rate while a key is held in a field.

#include "base/clock.hpp"

class Input;

Clock::Seconds imguiNextFrameIn(const Input& input, Clock::TimePoint frameStart);

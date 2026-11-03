#include "app/imgui-frames.hpp"
#include "app/input.hpp"
#include "base/frames.hpp"

#include <imgui.h>
#include <algorithm>

namespace {

// A key held that repeats while typing (not a modifier).
bool editingKeyHeld(const InputFrame& input)
{
    for (Key k : input.held)
        if (!isModifierKey(k)) return true;
    return false;
}

}

Clock::Seconds imguiNextFrameIn(const Input& input, Clock::TimePoint frameStart)
{
    constexpr Clock::Seconds kTick{ 0.1 };          // 10 frames a second
    constexpr Clock::Seconds kTooltipRest{ 0.6 };   // every ImGui tooltip delay is shorter
    if (input.imguiHolding()) return kFrameNow;     // events wait for the next frame
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return editingKeyHeld(input.frame()) ? Clock::Seconds(double(io.KeyRepeatRate)) : kTick;
    const Clock::Seconds sinceMove = std::max(Clock::Seconds(0), Clock::between(input.lastMove(), frameStart));
    if (ImGui::IsAnyItemHovered() && sinceMove < kTooltipRest) return kTick;
    return kNoFrame;
}

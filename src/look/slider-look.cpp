#include "look/slider-look.hpp"

#include <algorithm>
#include <cmath>

namespace SliderLook {

namespace {

constexpr ImU32 kGrab    = Palette::kGrab.packed();
constexpr ImU32 kGrabHot = Palette::kGrabHot.packed();
constexpr ImU32 kGrabRim = Palette::kGrabRim.packed();
constexpr float kRim     = 1.0f;   // pixels, at any scale

float grabHalfWidth(float unit) { return std::max(1.0f, std::round(3.0f * unit)); }

}

float trackThickness(float unit) { return std::max(2.0f, std::round(3.0f * unit)); }

float grabReach(float unit) { return grabHalfWidth(unit) + kRim; }

void drawTrack(ImDrawList* dl, float x0, float x1, float cy, float xv, float unit, ImU32 track)
{
    const float t = trackThickness(unit);
    dl->AddRectFilled(ImVec2(x0, cy - t * 0.5f), ImVec2(x1, cy + t * 0.5f), track, t * 0.5f);
    dl->AddRectFilled(ImVec2(x0, cy - t * 0.5f), ImVec2(xv, cy + t * 0.5f), kTrackFill, t * 0.5f);
}

void drawGrab(ImDrawList* dl, float x, float cy, float unit, bool hot, float halfHeight)
{
    const float gw = grabHalfWidth(unit), gh = halfHeight > 0.0f ? halfHeight : std::max(2.0f, std::round(6.5f * unit));
    dl->AddRectFilled(ImVec2(x - gw - kRim, cy - gh - kRim), ImVec2(x + gw + kRim, cy + gh + kRim), kGrabRim, 2.0f * unit);
    dl->AddRectFilled(ImVec2(x - gw, cy - gh), ImVec2(x + gw, cy + gh), hot ? kGrabHot : kGrab, 1.5f * unit);
}

}

#pragma once
// A color as hue, saturation and brightness (0..1 each): how the pickers edit one (the Color
// panel's wheel, FloatColorPick).

#include <imgui.h>

namespace Hsv {

ImU32 packed(float h, float s, float v, float a = 1.0f);   // to draw with

// A hue slider's gradient: the full circle at full saturation and brightness.
constexpr int kHueStops = 7;
void hueStops(ImU32 (&stops)[kHueStops]);

// h, s, v following `rgb`. Grey or black has no hue: the one there stays; black has no saturation either.
void follow(const float rgb[3], float& h, float& s, float& v);

}

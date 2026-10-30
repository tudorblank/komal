#pragma once
// How the views navigate, the same everywhere: the canvas, the navigator and the graph read these.

namespace Navigation {

constexpr float kWheelPanStep   = 24.0f;   // points per wheel notch
constexpr float kWheelZoomStep  = 1.1f;    // per Ctrl / Cmd + wheel notch: the canvas and the navigator
constexpr float kGraphZoomStep  = 1.2f;    // ... the graph's (its zoom range is far narrower: 60x against 1280x)
constexpr float kZoomButtonStep = 1.25f;   // per - / + click
constexpr float kRotateStep     = 15.0f;   // degrees per click

}

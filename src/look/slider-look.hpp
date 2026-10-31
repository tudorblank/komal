#pragma once
// The one slider look: a thin track filled up to the value in the slider blue, under a small pill
// grab with a dark rim. The panels' sliders (ui/widgets) and the nodes' (graph/painter) both draw
// it; sizes are in `unit`s (a panel's UI unit, or the graph's zoom), so they match at any scale.

#include "base/palette.hpp"

#include <imgui.h>

namespace SliderLook {

inline constexpr ImU32 kTrack     = Palette::kTrack.packed();       // on a panel
inline constexpr ImU32 kTrackFill = Palette::kAccentDim.packed();

float trackThickness(float unit);
// The track from x0 to x1 at cy, filled up to xv, in `track`'s grey (the nodes pass their darker one).
void drawTrack(ImDrawList* dl, float x0, float x1, float cy, float xv, float unit, ImU32 track = kTrack);
// The grab, centred on (x, cy). halfHeight 0: the standard; a box slider's is taller.
void drawGrab(ImDrawList* dl, float x, float cy, float unit, bool hot, float halfHeight = 0.0f);
// Half the grab's width, its rim included: how far it reaches either side of the value.
float grabReach(float unit);

}

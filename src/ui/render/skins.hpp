#pragma once
// How each control looks, drawn from its style (ui/style) and its state this frame (hot, on, its
// value's place): the rendering half of every widget. Widgets (ui/widgets) work out the state and
// call these; the graph draws its node sliders with the same skin at its zoom. A skin decides no
// sizes or colours of its own: they're all its style's.
//
//   Skin::slider(Painter::window(), Style::get().slider, x0, x1, cy, valueX, hot);

#include "ui/render/painter.hpp"
#include "ui/style/ui-style.hpp"

namespace Skin {

enum class Icon : unsigned char { Minus, Plus, RotateLeft, RotateRight };
enum class Figure : unsigned char { Ramp, Wave };   // a thick slider's figure: zoom grows, rotation swings about the middle

// ---- boxes: fields and buttons
void box(const Painter& p, const Style::BoxStyle& s, ImVec2 a, ImVec2 b, bool hot);
void centeredText(const Painter& p, ImVec2 a, ImVec2 b, ImU32 color, const char* text, const char* end = nullptr);   // clipped to a..b
void button(const Painter& p, const Style::BoxStyle& s, ImVec2 a, ImVec2 b, bool hot, const char* label, const char* end = nullptr);
// A value in a box, centred; `prefix` dim before it (a channel's letter).
void valueBox(const Painter& p, const Style::BoxStyle& s, ImVec2 a, ImVec2 b, bool hot, const char* text, const char* prefix = nullptr);
// Text in a box, from its left.
void textBox(const Painter& p, const Style::BoxStyle& s, ImVec2 a, ImVec2 b, bool hot, const char* text);
void iconButton(const Painter& p, const Style::BoxStyle& box, const Style::IconStyle& icon, ImVec2 a, ImVec2 b, bool hot, Icon shape);
void swatch(const Painter& p, const Style::SwatchStyle& s, ImVec2 a, ImVec2 b, ImU32 color, bool hot);

// ---- toggles: the mark centred on `center`, and its label after it
void radio(const Painter& p, const Style::ToggleStyle& s, ImVec2 center, bool on, bool hot);
void checkbox(const Painter& p, const Style::ToggleStyle& s, ImVec2 center, bool on, bool hot);
void toggleLabel(const Painter& p, const Style::ToggleStyle& s, ImVec2 at, const char* label, const char* end);

// ---- sliders: the track from x0 to x1 at cy, its value at xv
void slider(const Painter& p, const Style::SliderStyle& s, float x0, float x1, float cy, float xv, bool hot);
void sliderGrab(const Painter& p, const Style::SliderStyle& s, float x, float cy, bool hot);
// `from`: where the covered part starts (a ramp: its left end; a wave: its middle).
void figureSlider(const Painter& p, const Style::FigureSliderStyle& s, const Style::BoxStyle& box, float x0, float x1, float cy,
                  float xv, float from, Figure figure, bool hot);
void gradientSlider(const Painter& p, const Style::GradientSliderStyle& s, float x0, float x1, float cy, float xv,
                    const ImU32* stops, int count, bool hot);

// ---- colour pickers
// A checkerboard of `cell`-sized squares, `a` at the corner: behind a colour that's see-through.
void checker(const Painter& p, ImVec2 a, ImVec2 b, float cell, ImU32 colorA, ImU32 colorB);
// The saturation (left to right) / brightness (top to bottom) square of a hue, its marker at s, v.
void colorSquare(const Painter& p, const Style::ColorPickerStyle& s, ImVec2 a, ImVec2 b, ImU32 pureHue, float sat, float value,
                 float markerRadius);
void colorMarker(const Painter& p, const Style::ColorPickerStyle& s, ImVec2 at, float radius);   // a light ring inside a dark one

// ---- the Toolbox's tool buttons: the button; the tool draws its own icon on it
void toolButton(const Painter& p, const Style::ToolButtonStyle& s, ImVec2 a, ImVec2 b, bool selected, bool hot);

// ---- the documents' strip: a tab, its name from the left, its mark at the right end (padding
// before it): the close cross, a dot in its place while it's unsaved, the cross in a box while
// the mouse is on it
enum class TabMark : unsigned char { Close, Unsaved, CloseHot };
void documentTab(const Painter& p, const Style::DocumentTabStyle& s, ImVec2 a, ImVec2 b, bool shown, bool hot, const char* name,
                 TabMark mark);

}

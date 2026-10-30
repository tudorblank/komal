#pragma once
// Every colour the app's look is made of, defined once. Plain data: the theme, the widgets and
// the canvas and graph engines all read their colours from here. (A view's own colours that
// mean something only there, like the graph's node headers, live with that view.)

#include <array>
#include <cstdint>

// === COLOR8 === //
// Straight (not premultiplied) 8-bit RGBA.
struct Color8 {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    // ImGui's ImU32 (IM_COL32's byte order).
    constexpr uint32_t packed() const { return uint32_t(a) << 24 | uint32_t(b) << 16 | uint32_t(g) << 8 | uint32_t(r); }
    constexpr Color8 withAlpha(uint8_t alpha) const { return { r, g, b, alpha }; }
    constexpr std::array<float, 4> floats() const { return { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f }; }
};

constexpr Color8 grey8(uint8_t v, uint8_t a = 255) { return { v, v, v, a }; }

// A packed colour (ImU32) with its alpha replaced.
constexpr uint32_t withAlpha(uint32_t packed, uint8_t alpha) { return (packed & 0x00FFFFFFu) | uint32_t(alpha) << 24; }

// === PALETTE === //
namespace Palette {

// ---- the accent: selections, focus, checked things, the parts a value covers
constexpr Color8 kAccent    = { 140, 180, 245 };   // #8CB4F5
constexpr Color8 kAccentDim = { 104, 136, 186 };   // a slider's filled part

// ---- sliders and toggles, on a panel
constexpr Color8 kTrack      = grey8(50);
constexpr Color8 kGrab       = grey8(205);
constexpr Color8 kGrabHot    = grey8(255);
constexpr Color8 kGrabRim    = grey8(10);
constexpr Color8 kToggleEdge = grey8(92);
constexpr Color8 kToggleHot  = grey8(140);
constexpr Color8 kCheckMark  = grey8(20);       // drawn on the accent

// ---- colour boxes and markers: readable on any colour
constexpr Color8 kDarkRim   = grey8(0, 200);    // around a swatch, a marker's outer ring
constexpr Color8 kLightRim  = grey8(255, 36);   // inside a swatch: a black one still shows

// ---- behind everything
constexpr Color8 kWindowBackground = grey8(14);   // the window, where no panel is
constexpr Color8 kCanvasBackground = grey8(19);   // a canvas view, before its checkerboard

// ---- pictures: behind their transparent parts
constexpr Color8 kCanvasCheckerA  = grey8(0x13);   // the canvas, at its origin
constexpr Color8 kCanvasCheckerB  = grey8(0x1F);
constexpr float  kCanvasChecker   = 8.0f;          // its squares, points (they keep their size on screen)
constexpr Color8 kPictureCheckerA = grey8(52);     // small pictures (navigator, thumbnails), at a corner
constexpr Color8 kPictureCheckerB = grey8(72);
constexpr float  kPictureChecker  = 8.0f;          // their squares, pixels

// ---- marks over a picture
constexpr Color8 kViewOutline = { 214, 72, 72 };   // what the canvas shows, on a map of the picture
constexpr Color8 kEmptyArea   = grey8(62);         // a picture not there yet
constexpr Color8 kFaintOutline = grey8(255, 90);   // where a dragged edit's area was
constexpr Color8 kOriginMark  = grey8(255, 115);   // the "+" on the canvas's 0, 0
constexpr Color8 kHudBack     = grey8(0, 140);     // a status line over the canvas
constexpr Color8 kHudText     = grey8(220);

// ---- notices (ui/notices.hpp): something went wrong
constexpr Color8 kNoticeBack = grey8(28, 235);
constexpr Color8 kNoticeMark = { 214, 72, 72 };

}

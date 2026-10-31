#pragma once
// Every colour the app's look is made of, defined once. Plain data: the theme, the widgets and
// the canvas and graph engines all read their colours from here. (A view's own colours that
// mean something only there, like the graph's node headers, live with that view.)

#include "base/color.hpp"

// === PALETTE === //
namespace Palette {

// ---- panels: their surfaces and text (ui/theme.cpp hands them to ImGui)
constexpr Color8 kText         = grey8(219);
constexpr Color8 kTextDim      = grey8(117);       // disabled, hints
constexpr Color8 kPanel        = grey8(22);        // a panel's background, its tab while shown
constexpr Color8 kBar          = grey8(17);        // title and menu bars, tabs
constexpr Color8 kPopup        = grey8(27, 250);
constexpr Color8 kBorder       = grey8(43, 204);   // around panels and popups, separators
constexpr Color8 kField        = grey8(33);        // value fields
constexpr Color8 kFieldHot     = grey8(41);
constexpr Color8 kFieldActive  = grey8(48);
constexpr Color8 kButton       = grey8(36);
constexpr Color8 kButtonHot    = grey8(46);
constexpr Color8 kButtonActive = grey8(56);
constexpr Color8 kRow          = grey8(38);        // a selected row in a list; a tab under the mouse
constexpr Color8 kRowHot       = grey8(48);
constexpr Color8 kRowActive    = grey8(56);
constexpr Color8 kRowAlt       = grey8(255, 5);    // every other table row
constexpr Color8 kTableHeader  = grey8(31);
constexpr Color8 kTableBorder  = grey8(43);
constexpr Color8 kTableLine    = grey8(33);
constexpr Color8 kScrollGrab       = grey8(56);
constexpr Color8 kScrollGrabHot    = grey8(69);
constexpr Color8 kScrollGrabActive = grey8(82);
constexpr Color8 kResizeGrip   = grey8(77, 51);
constexpr Color8 kPlot         = grey8(153);
constexpr Color8 kNavHighlight = grey8(255, 179);   // the window picked with Ctrl+Tab
constexpr Color8 kNavDim       = grey8(0, 102);     // ... and behind it
constexpr Color8 kModalDim     = grey8(0, 128);     // behind a modal window
constexpr Color8 kNone         = grey8(0, 0);

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
constexpr Color8 kWindowBackground = grey8(14);   // the window, where no panel is (docking's empty space too)
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

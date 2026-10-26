#pragma once

#include <imgui.h>

namespace GraphStyle {

// ---- node geometry
constexpr float kNodeWidth  = 196.0f;
constexpr float kHeaderH    = 24.0f;
constexpr float kRounding   = 4.0f;
constexpr float kBoxGap     = 5.0f;    // between a node's boxes (body, properties, signals)
constexpr float kPad        = 8.0f;
constexpr float kIoRowH     = 22.0f;   // "Input ... Output" row above the thumbnail
constexpr float kThumbW     = 164.0f;  // 4:3, like the thumbnail textures
constexpr float kThumbH     = 123.0f;
constexpr float kThumbBelow = 13.0f;
constexpr float kRowH       = 20.0f;   // property, section and channel rows
constexpr float kLayerRowH  = 34.0f;
constexpr float kLayerGap   = 3.0f;
constexpr float kPortRadius = 5.0f;
constexpr float kPortHit    = 9.0f;    // grab radius
constexpr float kIconSize   = 14.0f;
constexpr float kGrid       = 32.0f;

// ---- text sizes
constexpr float kTitleText = 15.0f;
constexpr float kText      = 14.0f;
constexpr float kSmallText = 12.5f;

// ---- canvas
constexpr ImU32 kCanvas      = IM_COL32( 20,  20,  20, 255);
constexpr ImU32 kGridLine    = IM_COL32(255, 255, 255,   7);
constexpr ImU32 kGridMajor   = IM_COL32(255, 255, 255,  13);
constexpr ImU32 kOrigin      = IM_COL32(255, 255, 255, 110);   // the "+" on the graph's 0, 0
constexpr ImU32 kRubberFill  = IM_COL32(140, 180, 245,  32);
constexpr ImU32 kRubberEdge  = IM_COL32(140, 180, 245, 190);

// ---- nodes
constexpr ImU32 kBody         = IM_COL32( 37,  37,  37, 255);
constexpr ImU32 kInset        = IM_COL32( 28,  28,  28, 255);   // thumbnail well, value fields
constexpr ImU32 kRow          = IM_COL32( 47,  47,  47, 255);   // layer rows
constexpr ImU32 kRowHovered   = IM_COL32( 56,  56,  56, 255);
constexpr ImU32 kBorder       = IM_COL32( 10,  10,  10, 255);
constexpr ImU32 kBorderHover  = IM_COL32(105, 105, 105, 255);
constexpr ImU32 kSelection    = IM_COL32(140, 180, 245, 255);
constexpr ImU32 kActiveRaster = IM_COL32(255, 160,  40, 255);   // the raster the canvas paints into
constexpr ImU32 kShadow       = IM_COL32(  0,   0,   0,  60);
constexpr ImU32 kSeparator    = IM_COL32(  0,   0,   0, 110);

constexpr ImU32 kHeaderRaster     = IM_COL32(131,  49,  74, 255);
constexpr ImU32 kHeaderReference  = IM_COL32( 36,  98, 131, 255);
constexpr ImU32 kHeaderMove       = IM_COL32( 92,  58, 140, 255);
constexpr ImU32 kHeaderCompositor = IM_COL32( 29, 114,  94, 255);
constexpr ImU32 kHeaderOutput     = IM_COL32(100,  36,  36, 255);

constexpr ImU32 kTitleColor  = IM_COL32(232, 232, 232, 255);
constexpr ImU32 kTextColor   = IM_COL32(214, 214, 214, 255);
constexpr ImU32 kDimText     = IM_COL32(148, 148, 148, 255);
constexpr ImU32 kIconOff     = IM_COL32(120, 120, 120, 255);
constexpr ImU32 kIconOn      = IM_COL32(240, 240, 240, 255);
constexpr ImU32 kAccent      = IM_COL32(140, 180, 245, 255);   // the theme's pastel blue

// ---- ports and links
constexpr ImU32 kImagePort  = IM_COL32(199, 199,  41, 255);   // image data
constexpr ImU32 kMaskPort   = IM_COL32(161, 161, 161, 255);   // greyscale
constexpr ImU32 kPortRing   = IM_COL32( 10,  10,  10, 255);
constexpr ImU32 kLinkReject = IM_COL32(255,  90,  90, 255);
constexpr float kLinkWidth  = 2.0f;   // pixels at 100%

// ---- sliders
constexpr ImU32 kTrack      = IM_COL32( 20,  20,  20, 255);
constexpr ImU32 kTrackFill  = IM_COL32(104, 136, 186, 255);   // the accent, dimmed
constexpr ImU32 kGrab       = IM_COL32(205, 205, 205, 255);
constexpr ImU32 kGrabActive = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kChannelColor[4] = {   // A, R, G, B
    IM_COL32(220, 220, 220, 255), IM_COL32(236, 112, 112, 255),
    IM_COL32(122, 206, 122, 255), IM_COL32(118, 152, 240, 255) };

}

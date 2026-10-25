#pragma once
// How the graph looks, in one place. Lengths are graph units (see GraphCamera); line widths
// that should stay thin on screen are in pixels and say so.

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
constexpr float kTitleText = 14.0f;
constexpr float kText      = 13.0f;
constexpr float kSmallText = 11.5f;

// ---- canvas
constexpr ImU32 kCanvas      = IM_COL32( 29,  29,  29, 255);
constexpr ImU32 kGridLine    = IM_COL32(255, 255, 255,   9);
constexpr ImU32 kGridMajor   = IM_COL32(255, 255, 255,  16);
constexpr ImU32 kRubberFill  = IM_COL32( 84, 140, 242,  40);
constexpr ImU32 kRubberEdge  = IM_COL32( 84, 140, 242, 200);

// ---- nodes
constexpr ImU32 kBody         = IM_COL32( 46,  46,  46, 255);
constexpr ImU32 kInset        = IM_COL32( 36,  36,  36, 255);   // thumbnail well, value fields
constexpr ImU32 kRow          = IM_COL32( 58,  58,  58, 255);   // layer rows
constexpr ImU32 kRowHovered   = IM_COL32( 68,  68,  68, 255);
constexpr ImU32 kBorder       = IM_COL32( 12,  12,  12, 255);
constexpr ImU32 kBorderHover  = IM_COL32(120, 120, 120, 255);
constexpr ImU32 kSelection    = IM_COL32( 84, 140, 242, 255);
constexpr ImU32 kActiveRaster = IM_COL32(255, 160,  40, 255);   // the raster the canvas paints into
constexpr ImU32 kShadow       = IM_COL32(  0,   0,   0,  60);
constexpr ImU32 kSeparator    = IM_COL32(  0,   0,   0, 110);

constexpr ImU32 kHeaderRaster     = IM_COL32(131,  49,  74, 255);
constexpr ImU32 kHeaderReference  = IM_COL32( 36,  98, 131, 255);
constexpr ImU32 kHeaderMove       = IM_COL32( 92,  58, 140, 255);
constexpr ImU32 kHeaderCompositor = IM_COL32( 29, 114,  94, 255);
constexpr ImU32 kHeaderOutput     = IM_COL32(100,  36,  36, 255);

constexpr ImU32 kTitleColor  = IM_COL32(240, 240, 240, 255);
constexpr ImU32 kTextColor   = IM_COL32(215, 215, 215, 255);
constexpr ImU32 kDimText     = IM_COL32(140, 140, 140, 255);
constexpr ImU32 kIconOff     = IM_COL32(120, 120, 120, 255);
constexpr ImU32 kIconOn      = IM_COL32(240, 240, 240, 255);
constexpr ImU32 kAccent      = IM_COL32( 84, 140, 242, 255);

// ---- ports and links
constexpr ImU32 kImagePort  = IM_COL32(199, 199,  41, 255);   // image data
constexpr ImU32 kMaskPort   = IM_COL32(161, 161, 161, 255);   // greyscale
constexpr ImU32 kPortRing   = IM_COL32( 10,  10,  10, 255);
constexpr ImU32 kLinkReject = IM_COL32(255,  90,  90, 255);
constexpr float kLinkWidth  = 2.0f;   // pixels at 100%

// ---- sliders
constexpr ImU32 kTrack      = IM_COL32( 26,  26,  26, 255);
constexpr ImU32 kTrackFill  = IM_COL32( 78, 112, 176, 255);
constexpr ImU32 kGrab       = IM_COL32(205, 205, 205, 255);
constexpr ImU32 kGrabActive = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kChannelColor[4] = {   // A, R, G, B
    IM_COL32(220, 220, 220, 255), IM_COL32(236, 112, 112, 255),
    IM_COL32(122, 206, 122, 255), IM_COL32(118, 152, 240, 255) };

} // namespace GraphStyle

#pragma once

#include "base/palette.hpp"

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
constexpr ImU32 kRubberFill  = Palette::kAccent.withAlpha(32).packed();
constexpr ImU32 kRubberEdge  = Palette::kAccent.withAlpha(190).packed();

// ---- nodes
constexpr ImU32 kBody         = IM_COL32( 37,  37,  37, 255);
constexpr ImU32 kInset        = IM_COL32( 28,  28,  28, 255);   // thumbnail well, value fields
constexpr ImU32 kRow          = IM_COL32( 47,  47,  47, 255);   // layer rows
constexpr ImU32 kRowHovered   = IM_COL32( 56,  56,  56, 255);
constexpr ImU32 kBorder       = IM_COL32( 10,  10,  10, 255);
constexpr ImU32 kBorderHover  = IM_COL32(105, 105, 105, 255);
constexpr ImU32 kSelection    = Palette::kAccent.packed();
constexpr ImU32 kActiveRaster = IM_COL32(255, 160,  40, 255);   // the raster the canvas paints into
constexpr ImU32 kShadow       = IM_COL32(  0,   0,   0,  60);
constexpr ImU32 kSeparator    = IM_COL32(  0,   0,   0, 110);

constexpr ImU32 kHeaderOutput     = IM_COL32(100,  36,  36, 255);   // the master (each kind's own: NodeKind::header)

constexpr ImU32 kTitleColor  = IM_COL32(232, 232, 232, 255);
constexpr ImU32 kTextColor   = IM_COL32(214, 214, 214, 255);
constexpr ImU32 kDimText     = IM_COL32(148, 148, 148, 255);
constexpr ImU32 kIconOff     = IM_COL32(120, 120, 120, 255);
constexpr ImU32 kIconOn      = IM_COL32(240, 240, 240, 255);
constexpr ImU32 kAccent      = Palette::kAccent.packed();

// ---- layer stacks
constexpr ImU32 kGapFill     = Palette::kAccent.withAlpha(36).packed();   // where a dragged link or layer lands
constexpr ImU32 kEmptyStack  = IM_COL32(255, 255, 255,  28);   // a stack with nothing in it: its frame
constexpr ImU32 kLiftShadow  = IM_COL32(  0,   0,   0,  90);   // under a layer row being dragged
constexpr ImU32 kChipEdge    = IM_COL32(255, 255, 255,  50);   // the blend mode chip
constexpr uint8_t kPlaceholderAlpha = 150;                     // a port that isn't there yet

// ---- ports and links
constexpr ImU32 kImagePort  = IM_COL32(199, 199,  41, 255);   // image data
constexpr ImU32 kMaskPort   = IM_COL32(161, 161, 161, 255);   // greyscale
constexpr ImU32 kPortRing   = IM_COL32( 10,  10,  10, 255);
constexpr ImU32 kLinkReject = IM_COL32(255,  90,  90, 255);
constexpr ImU32 kImageLink  = withAlpha(kImagePort, 220);
constexpr ImU32 kMaskLink   = withAlpha(kMaskPort, 220);
constexpr ImU32 kLinkLoose  = IM_COL32(230, 230, 230, 170);   // dragged, over nothing it can land on
constexpr ImU32 kLinkGlow   = IM_COL32(255, 255, 255,  70);   // under a hovered link
constexpr ImU32 kFadingGlow = IM_COL32(255, 255, 255,  50);   // ... a link into the master
constexpr float kLinkWidth  = 2.0f;   // pixels at 100%

// ---- a link's insert button
constexpr ImU32 kInsertFill = IM_COL32( 44,  44,  44, 255);
constexpr ImU32 kInsertRim  = IM_COL32(220, 220, 220, 255);
constexpr ImU32 kInsertPlus = IM_COL32(240, 240, 240, 255);

// ---- the minimap
constexpr ImU32 kMapBack     = IM_COL32( 14,  14,  14, 210);
constexpr ImU32 kMapFrame    = IM_COL32( 46,  46,  46, 255);
constexpr ImU32 kMapNode     = IM_COL32( 78,  78,  78, 255);
constexpr ImU32 kMapEdge     = IM_COL32( 96,  96,  96, 255);
constexpr ImU32 kMapMaskEdge = IM_COL32( 70,  70,  70, 255);

// ---- sliders
constexpr ImU32 kTrack      = IM_COL32( 20,  20,  20, 255);
constexpr ImU32 kTrackFill  = Palette::kAccentDim.packed();
constexpr ImU32 kGrab       = Palette::kGrab.packed();
constexpr ImU32 kGrabActive = Palette::kGrabHot.packed();
constexpr ImU32 kChannelColor[4] = {   // A, R, G, B
    IM_COL32(220, 220, 220, 255), IM_COL32(236, 112, 112, 255),
    IM_COL32(122, 206, 122, 255), IM_COL32(118, 152, 240, 255) };

}

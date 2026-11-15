#pragma once
// Where things sit in the graph, in graph units: a node's width, its rows and boxes, the grid.
// Laying nodes out (node-item.cpp), hitting them (hit.cpp) and painting them read these; how they look
// is ui/style/graph-style.hpp.

namespace GraphLayout {

constexpr float kNodeWidth  = 196.0f;
constexpr float kHeaderH    = 24.0f;
constexpr float kBoxGap     = 5.0f;    // between a node's boxes (body, properties, signals)
constexpr float kPad        = 8.0f;
constexpr float kIoRowH     = 22.0f;   // "Input ... Output" row above the thumbnail
constexpr float kThumbW     = 164.0f;  // 4:3, like the thumbnail textures
constexpr float kThumbH     = 123.0f;
constexpr float kThumbBelow = 13.0f;
constexpr float kRowH       = 20.0f;   // property, section and channel rows
constexpr float kLayerRowH  = 34.0f;
constexpr float kArtboardRowH = 26.0f;
constexpr float kArtboardIndent = 12.0f;   // its layers, and their ports
constexpr float kDropLineH  = 3.0f;    // the line a drop at the bottom, under an artboard, shows
constexpr float kArtboardEdge = 0.3f;  // the top of an artboard's row a drop goes above it from (the rest: onto it), under another artboard
constexpr float kStackBleed = 10.0f;   // a link takes the stack this far above and below its rows
constexpr float kLayerGap   = 3.0f;
// Room a drop opens between rows. Shorter than the shortest row (an artboard's): closing it moves
// the rows under it up by less than one, so the row under the mouse stays under it.
constexpr float kRoomH      = 14.0f;
constexpr float kIconSize   = 14.0f;
constexpr float kGrid       = 32.0f;

// inside a node's rows
constexpr float kStackInset     = 6.0f;    // a stack's rows inside the node's edges (their ports on the edge)
constexpr float kPropertyPad    = 4.0f;    // above and below a node's property rows
constexpr float kStackPad       = 4.0f;    // ... a stack's rows
constexpr float kSignalsPad     = 3.0f;    // ... the signals box's sections
constexpr float kSectionInset   = 4.0f;    // a section's fold widget inside the node's edges
constexpr float kIoLabelInset   = 4.0f;    // "Input" / "Output" this far past kPad
constexpr float kLockGap        = 6.0f;    // between the lock and the alpha lock
constexpr float kArrowSize      = 7.0f;    // a section's or an artboard's fold arrow
constexpr float kPropertyLabelW = 56.0f;   // a property's label, before its value
constexpr float kValueInset     = 2.0f;    // a value inside its row, top and bottom
constexpr float kTrackLeft      = 16.0f;   // a channel's slider track from the node's left edge (its mask port there)
constexpr float kTrackRight     = 50.0f;   // ... and from its right edge (the value and the channel's letter there)
constexpr float kTrackInset     = 3.0f;    // ... inside its row, top and bottom
constexpr float kTrackReach     = 5.0f;    // the track's widget, past each end of it
constexpr float kRowEndInset    = 2.0f;    // the channel value's widget, before the node's right edge
constexpr float kSectionArrowX  = 12.0f;   // a section's arrow centre, from its left
constexpr float kSectionLabelX  = 21.0f;   // ... its name
constexpr float kChannelValueX  = 20.0f;   // a channel row's value ends this far from its right ...
constexpr float kChannelLetterX = 13.0f;   // ... its letter starts this far
constexpr float kRowPad         = 6.0f;    // a stack row's content ends this far before its right edge
constexpr float kChipW          = 46.0f;   // a layer row's blend chip, at its right
constexpr float kChipHalfH      = 8.0f;
constexpr float kChipGap        = 4.0f;    // a layer's name stops this far before the chip
constexpr float kRowNameGap     = 6.0f;    // ... starts this far after its thumbnail
constexpr float kArtboardNameGap = 7.0f;   // an artboard's name after its color
constexpr float kLayerEyeX      = 6.0f;    // a layer row's eye from its left
constexpr float kLayerThumbGap  = 5.0f;    // ... its thumbnail after the eye
constexpr float kLayerThumbInset = 3.0f;   // ... and inside the row, top and bottom
constexpr float kArtboardFoldX  = 3.0f;    // an artboard row's fold arrow from its left
constexpr float kArtboardEyeGap = 4.0f;    // ... its eye after the arrow
constexpr float kArtboardColorGap = 6.0f;  // ... its color after the eye

}

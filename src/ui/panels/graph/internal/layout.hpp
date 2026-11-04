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

}

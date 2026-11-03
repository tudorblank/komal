#pragma once
// Where things sit in the graph, in graph units: a node's width, its rows and boxes, the grid.
// Laying nodes out (item.cpp), hitting them (hit.cpp) and painting them read these; how they look
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
constexpr float kLayerGap   = 3.0f;
constexpr float kIconSize   = 14.0f;
constexpr float kGrid       = 32.0f;

}

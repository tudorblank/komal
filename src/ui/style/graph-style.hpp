#pragma once
// The graph view's look: what its canvas, nodes, ports, links and minimap are drawn in, made of
// the palette's colours (palette.hpp), and the sizes that are only looks (text, corners, ports, links).
// Where things sit in a node is the graph's own layout (panels/graph/internal/layout.hpp); its
// sliders are the app's (Style::slider at the graph's zoom, Tone::Node).

#include <cstdint>

enum class NodeType : uint8_t;   // node/node.hpp

#include <imgui.h>

namespace GraphStyle {

// ---- sizes, in graph units
constexpr float kRounding   = 4.0f;    // a node's corners
constexpr float kPortRadius = 5.0f;
constexpr float kTitleText  = 15.0f;
constexpr float kText       = 14.0f;
constexpr float kSmallText  = 12.5f;
constexpr float kHintText   = 0.85f;   // the graph's help line, of the UI font
constexpr float kLinkWidth  = 2.0f;    // pixels at 100%
constexpr uint8_t kPlaceholderAlpha = 150;   // a port that isn't there yet

// ---- colours: made from the palette (graph-style.cpp), again when the theme changes
struct Colors {
    // the canvas
    ImU32 canvas;
    ImU32 gridLine;
    ImU32 gridMajor;
    ImU32 origin;
    ImU32 rubberFill;
    ImU32 rubberEdge;
    // nodes
    ImU32 body;
    ImU32 inset;
    ImU32 row;
    ImU32 rowHovered;
    ImU32 rowSelected;      // a stack row picked: over its row ...
    ImU32 selectedText;     // ... its name ...
    ImU32 selectedEdge;     // ... its blend chip
    ImU32 originEdge;       // a row a drag carries: its spot's dashed outline
    ImU32 swatchCheckerA;   // a transparent colour's square: a checkerboard, as behind a thumbnail
    ImU32 swatchCheckerB;
    ImU32 border;
    ImU32 borderHover;
    ImU32 selection;
    ImU32 activeRaster;
    ImU32 shadow;
    ImU32 separator;
    ImU32 rasterHeader;
    ImU32 referenceHeader;
    ImU32 moveHeader;
    ImU32 compositorHeader;
    ImU32 headerOutput;
    ImU32 title;
    ImU32 text;
    ImU32 dimText;
    ImU32 iconOff;
    ImU32 iconOn;
    ImU32 accent;
    ImU32 channel[4];   // A, R, G, B
    // layer stacks
    ImU32 gapFill;   // where a dragged link or layer lands
    ImU32 emptyStack;
    ImU32 liftShadow;
    ImU32 chipEdge;
    // ports and links
    ImU32 imagePort;
    ImU32 maskPort;
    ImU32 portRing;
    ImU32 imageLink;
    ImU32 maskLink;
    ImU32 linkLoose;
    ImU32 linkGlow;
    ImU32 fadingGlow;
    ImU32 insertFill;
    ImU32 insertRim;
    ImU32 insertPlus;
    // the minimap
    ImU32 mapBack;
    ImU32 mapFrame;
    ImU32 mapNode;
    ImU32 mapEdge;
    ImU32 mapMaskEdge;
};

const Colors& colors();             // the palette's, as it is now
ImU32 kindHeader(NodeType type);   // a node's title bar, by its kind (the master has its own)

}

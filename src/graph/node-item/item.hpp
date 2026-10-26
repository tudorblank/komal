#pragma once
// A node as the graph draws it (not the document Node): every box, port and control of one node, laid out in graph space from
// the node's type and state. It is the single source of truth for drawing (painter) and
// for hit testing (panel), so the two can never disagree.
//
//   ┌ Title ─────────────┐   header band + body: the node's own I/O and its thumbnail
//   │ Input       Output ○
//   │   ┌──────────────┐ │
//   │   │    thumb     │ │
//   │   └──────────────┘ │
//   └────────────────────┘
//   ┌────────────────────┐   properties (only nodes that have some): Move offset,
//   ○ thumb  Layer   ... │   a compositor's layer stack (one input port per layer; empty, a
//   ┆   drop here        ┆   drop zone). While a link or a layer is dragged over a stack, a
//   ○ thumb  Layer   ... │   gap opens where it would land, wherever the mouse is.
//   └────────────────────┘
//   ┌────────────────────┐   signals: Main (+ Wet and Dry on effect nodes), each collapsible.
//   │ v Main             │   Collapsed shows alpha (and any masked channel), expanded all four.
//   ○ ───────[]──  100 A │   Every channel row has a mask input port on the left edge.
//   ○ ───[]──────   50 R │
//   └────────────────────┘
//
// The master output has no body (it has no output and the canvas is its picture): its
// header sits on top of its layer stack. Layers are added by dropping into the stack itself:
// there is no separate "add" input.

#include "core/project.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // ImRect: plain geometry, stable across ImGui versions
#include <vector>

struct NodeItemPort {
    ImVec2    pos;                  // on the box edge
    bool      output = false;
    InputSlot slot;                 // inputs: where a link lands
    bool      mask        = false;  // greyscale input
    bool      placeholder = false;  // drawn faint
};

enum class NodeItemControl : uint8_t {
    Title,           // double-click renames
    Lock,            // raster: lock pixels
    LockAlpha,       // raster: lock transparency (paint only where there are pixels)
    SectionToggle,   // collapse / expand a signal
    Scale,           // a signal channel's scale slider
    OffsetX, OffsetY,
    Layer,           // a stack row: drag to reorder
};

struct NodeItemWidget {
    NodeItemControl control;
    ImRect      rect;
    NodeSignal  signal  = NodeSignal::Main;
    NodeChannel channel = NodeChannel::A;
    uint32_t    layer   = 0;
};

struct NodeItemBox {
    ImRect rect;
    float  header = 0.0f;   // height of the header band (first box only)
};

struct NodeItemSectionRow { ImRect rect; NodeSignal signal; bool expanded; };
struct NodeItemChannelRow { ImRect rect; ImRect track; NodeSignal signal; NodeChannel channel; };
struct NodeItemLayerRow {
    enum class Kind : uint8_t { Layer, Gap, Empty };   // Gap: where a drop lands (layer = position); Empty: drop zone
    ImRect   rect;
    uint32_t layer = 0;
    Kind     kind  = Kind::Layer;
};

// Per-node view state the graph keeps (not part of the document).
struct NodeItemState {
    bool expanded[kSignalCount] = {};   // by NodeSignal; collapsed until the user opens one
};

struct NodeItem {
    NodeId id = kNoNodeId;
    std::vector<NodeItemBox>        boxes;     // [0] carries the header
    ImRect header;
    ImRect ioRow;                 // empty for the master
    ImRect thumb;                 // empty when the node has none
    ImRect offsetRow[2];          // Move: X, Y
    std::vector<NodeItemPort>       ports;
    std::vector<NodeItemWidget>     widgets;
    std::vector<NodeItemSectionRow> sections;
    std::vector<NodeItemChannelRow> channels;
    std::vector<NodeItemLayerRow>   layers;   // top of the stack first
    ImRect bounds;

    const NodeItemPort* output() const;
    const NodeItemPort* input(const InputSlot& slot) const;
};

// A stack laid out around a drag: `hidden` (a layer being dragged) is left out, and a gap opens
// at `gap`, a position among the layers shown (0 = bottom). -1: none.
struct StackLayout {
    int gap    = -1;
    int hidden = -1;
};

NodeItem buildNodeItem(const Project& project, const Node& n, const NodeItemState& view, StackLayout stack = {});

// A layer row's thumbnail, inside its rect.
ImRect layerThumbRect(const ImRect& row);

// The slot a link dropped on the node's body connects to (none: false).
bool preferredInput(const Node& n, InputSlot& out);

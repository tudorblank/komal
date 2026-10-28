#pragma once
// A node as the graph draws it (not the document Node): every box, port and control, laid out
// in graph space. Drawing (painter) and hit testing (panel) both read it, so they always agree.
//
//   ┌ Title ─────────────┐   header + body: I/O and thumbnail
//   │ Input       Output ○
//   │   ┌──────────────┐ │
//   │   │    thumb     │ │
//   │   └──────────────┘ │
//   └────────────────────┘
//   ┌────────────────────┐   properties: Move offset, or a layer stack (a port per layer;
//   ○ thumb  Layer   ... │   a gap opens where a dragged link or layer would land)
//   ┆   drop here        ┆
//   ○ thumb  Layer   ... │
//   └────────────────────┘
//   ┌────────────────────┐   signals: Main (+ Wet, Dry), collapsible. Collapsed: alpha and any
//   │ v Main             │   masked channel. Each channel row has a mask port.
//   ○ ───────[]──  100 A │
//   ○ ───[]──────   50 R │
//   └────────────────────┘
//
// The master has no body (no output; the canvas is its picture): its header sits on its stack.

#include "core/project.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // ImRect
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

// A stack laid out around a drag: `hidden` layer left out, a gap at position `gap` (0 = bottom). -1: none.
struct StackLayout {
    int gap    = -1;
    int hidden = -1;
    bool operator==(const StackLayout&) const = default;
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
    StackLayout stack;            // the drag layout it was built with

    const NodeItemPort* output() const;
    const NodeItemPort* input(const InputSlot& slot) const;
    void moveTo(float x, float y);   // the node moved: same layout, shifted
};


// Lays `n` out into `out`, reusing its storage.
void buildNodeItem(const Project& project, const Node& n, const NodeItemState& view, StackLayout stack, NodeItem& out);

// A layer row's thumbnail, inside its rect.
ImRect layerThumbRect(const ImRect& row);

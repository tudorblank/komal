#pragma once
// The shape of a node: every box, port and control of one node, laid out in graph space from
// the node's type and state. It is the single source of truth for drawing (node-painter) and
// for hit testing (graph-panel), so the two can never disagree.
//
//   ┌ Title ─────────────┐   header band + body: the node's own I/O and its thumbnail
//   │ Input       Output ○
//   │   ┌──────────────┐ │
//   │   │    thumb     │ │
//   │   └──────────────┘ │
//   └────────────────────┘
//   ┌────────────────────┐   properties (only nodes that have some): Move offset,
//   ○ thumb  Layer   ...  │   a compositor's layer stack (one input port per layer)
//   └────────────────────┘
//   ┌────────────────────┐   signals: Main (+ Wet and Dry on effect nodes), each collapsible.
//   │ v Main             │   Collapsed shows alpha (and any masked channel), expanded all four.
//   ○ ───────[]──  100 A │   Every channel row has a mask input port on the left edge.
//   ○ ───[]──────   50 R │
//   └────────────────────┘
//
// The master output has no body (it has no output and the canvas is its picture): its
// header sits on top of its layer stack.

#include "core/project.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImRect: plain geometry, stable across ImGui versions
#include <vector>

struct NodePort {
    ImVec2    pos;                  // on the box edge
    bool      output = false;
    InputSlot slot;                 // inputs: where a link lands
    bool      mask        = false;  // greyscale input
    bool      placeholder = false;  // the "new layer" slot of a stack
};

enum class NodeControl : uint8_t {
    Title,           // double-click renames
    Lock,            // raster: lock pixels
    LockAlpha,       // raster: lock transparency (paint only where there are pixels)
    SectionToggle,   // collapse / expand a signal
    Scale,           // a signal channel's scale slider
    OffsetX, OffsetY,
    Layer,           // a stack row: drag to reorder
};

struct NodeWidget {
    NodeControl control;
    ImRect      rect;
    NodeSignal  signal  = NodeSignal::Main;
    NodeChannel channel = NodeChannel::A;
    uint32_t    layer   = 0;
};

struct NodeBox {
    ImRect rect;
    float  header = 0.0f;   // height of the header band (first box only)
};

struct NodeSectionRow { ImRect rect; NodeSignal signal; bool expanded; };
struct NodeChannelRow { ImRect rect; ImRect track; NodeSignal signal; NodeChannel channel; };
struct NodeLayerRow   { ImRect rect; ImRect thumb; uint32_t layer; bool placeholder; };

// Per-node view state the graph keeps (not part of the document).
struct NodeViewState {
    bool expanded[kSignalCount] = {};   // by NodeSignal; collapsed until the user opens one
};

struct NodeShape {
    NodeId id = kNoNodeId;
    std::vector<NodeBox> boxes;   // [0] carries the header
    ImRect header;
    ImRect ioRow;                 // empty for the master
    ImRect thumb;                 // empty when the node has none
    ImRect offsetRow[2];          // Move: X, Y
    std::vector<NodePort>       ports;
    std::vector<NodeWidget>     widgets;
    std::vector<NodeSectionRow> sections;
    std::vector<NodeChannelRow> channels;
    std::vector<NodeLayerRow>   layers;   // top of the stack first, placeholder first of all
    ImRect bounds;

    const NodePort* output() const;
    const NodePort* input(const InputSlot& slot) const;
};

NodeShape buildNodeShape(const Project& project, const Node& n, const NodeViewState& view);

// The slot a link dropped on the node's body connects to (none: false).
bool preferredInput(const Node& n, InputSlot& out);

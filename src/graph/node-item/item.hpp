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
//   ┌────────────────────┐   properties: its kind's settings (a Move's offset), or a layer stack (a port per layer;
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

#include "document/input-slot.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // ImRect
#include <optional>
#include <variant>
#include <vector>

class Project;

struct NodeItemPort {
    ImVec2    pos;                  // on the box edge
    bool      output = false;
    InputSlot slot;                 // inputs: where a link lands
    bool      mask        = false;  // greyscale input
    bool      placeholder = false;  // drawn faint
};

// What a widget does: one type per control, each holding what it acts on.
struct TitleControl     { bool operator==(const TitleControl&) const = default; };       // double-click renames
struct LockControl      { bool operator==(const LockControl&) const = default; };        // RasterSettings: lock pixels
struct LockAlphaControl { bool operator==(const LockAlphaControl&) const = default; };   // ... lock transparency
struct SectionControl {                                                                  // collapse / expand a signal
    NodeSignal signal;
    bool operator==(const SectionControl&) const = default;
};
struct ScaleControl {                                                                    // a signal channel's scale slider
    NodeSignal  signal;
    NodeChannel channel;
    bool operator==(const ScaleControl&) const = default;
};
struct OffsetControl {                                                                   // MoveSettings: the offset, 0 x / 1 y
    int axis;
    bool operator==(const OffsetControl&) const = default;
};
struct LayerControl {                                                                    // a stack row: drag to reorder
    uint32_t layer;
    bool operator==(const LayerControl&) const = default;
};
using NodeItemControl = std::variant<TitleControl, LockControl, LockAlphaControl, SectionControl, ScaleControl, OffsetControl, LayerControl>;

struct NodeItemWidget {
    NodeItemControl control;
    ImRect          rect;
    template <class T> const T* as() const { return std::get_if<T>(&control); }
};

// A settings value's row in the properties box: its label; the value is a widget on it.
struct NodeItemPropertyRow {
    ImRect      rect;
    const char* label;
};

struct NodeItemBox {
    ImRect rect;
    float  header = 0.0f;   // height of the header band (first box only)
};

struct NodeItemSectionRow { ImRect rect; NodeSignal signal; bool expanded; };
struct NodeItemChannelRow { ImRect rect; ImRect track; NodeSignal signal; NodeChannel channel; };
struct NodeItemLayerRow {
    struct Layer { uint32_t index; bool operator==(const Layer&) const = default; };   // a layer of the stack
    struct Gap   { uint32_t at;    bool operator==(const Gap&) const = default; };     // where a dragged link or layer lands: a position
    struct Empty { bool operator==(const Empty&) const = default; };                   // an empty stack's drop zone
    ImRect rect;
    std::variant<Layer, Gap, Empty> kind;
    template <class T> const T* as() const { return std::get_if<T>(&kind); }
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
    std::optional<ImRect> ioRow;  // its body (none: the master)
    std::optional<ImRect> thumb;
    std::vector<NodeItemPropertyRow> properties;   // its kind's settings, one row per value
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

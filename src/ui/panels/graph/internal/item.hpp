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
//   ○ eye thumb Layer .. │   a gap opens where a dragged link or layer would land). The master's artboards are
//   │ v eye ■ Artboard 1 │   groups of its rows: a header (fold, eye, colour, name), its layers indented under it
//   │  ○ eye thumb Layer │   with their ports
//   ┆   drop here        ┆
//   └────────────────────┘
//   ┌────────────────────┐   signals: Main (+ Wet, Dry), collapsible. Collapsed: alpha and any
//   │ v Main             │   masked channel. Each channel row has a mask port.
//   ○ ───────[]──  100 A │
//   ○ ───[]──────   50 R │
//   └────────────────────┘
//
// The master has no body (no output; the canvas is its picture): its header sits on its stack.

#include "node/node.hpp"
#include "ui/panels/graph/internal/stack-drop.hpp"

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
struct LayerControl {                                                                    // a stack row: click picks, drag moves
    uint32_t layer;
    bool operator==(const LayerControl&) const = default;
};
struct LayerMuteControl {                                                                // ... its eye: mute it here
    uint32_t layer;
    bool operator==(const LayerMuteControl&) const = default;
};
struct ArtboardControl {                                                                 // an artboard's row (the master's): like a layer's
    ArtboardId artboard;
    bool operator==(const ArtboardControl&) const = default;
};
struct ArtboardFoldControl {                                                             // ... its triangle: fold its layers away
    ArtboardId artboard;
    bool operator==(const ArtboardFoldControl&) const = default;
};
struct ArtboardMuteControl {                                                             // ... its eye
    ArtboardId artboard;
    bool operator==(const ArtboardMuteControl&) const = default;
};
struct ArtboardColorControl {                                                            // ... its colour: the colour picker
    ArtboardId artboard;
    bool operator==(const ArtboardColorControl&) const = default;
};
using NodeItemControl = std::variant<TitleControl, LockControl, LockAlphaControl, SectionControl, ScaleControl, OffsetControl, LayerControl,
                                     LayerMuteControl, ArtboardControl, ArtboardFoldControl, ArtboardMuteControl, ArtboardColorControl>;

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
    struct Layer    { uint32_t index; bool operator==(const Layer&) const = default; };      // a layer of the stack
    struct Artboard {                                                                         // an artboard's header
        ArtboardId key;
        bool       folded;
        uint32_t   layers;   // in it (but the ones a drag carries), shown or folded away
        bool operator==(const Artboard&) const = default;
    };
    struct Gap {                       // room where a dragged link or row lands
        bool line;                     // the very bottom under an artboard: a thick line
        bool operator==(const Gap&) const = default;
    };
    struct Empty    { bool target; bool operator==(const Empty&) const = default; };          // an empty stack's drop zone (target: a drop lands on it)
    struct Origin {                                                                           // a row a drag carries: its spot, kept
        ArtboardId artboard = kNoArtboard;   // a carried artboard's (its row and its layers'), none: a layer's
        bool operator==(const Origin&) const = default;
    };
    ImRect rect;
    std::variant<Layer, Artboard, Gap, Empty, Origin> kind;
    ArtboardId inside   = kNoArtboard;   // the artboard it's in (indented)
    bool       selected = false;         // picked (NodeItemState::selected)
    template <class T> const T* as() const { return std::get_if<T>(&kind); }
};

// Per-node view state the graph keeps (not part of the document).
struct NodeItemState {
    bool expanded[kSignalCount] = {};   // by NodeSignal; collapsed until the user opens one
    std::vector<StackRowRef> selected;  // its stack's rows picked: what a drag moves together
    StackRowRef anchor;                 // where a Shift+click range starts
    std::vector<ArtboardId> folded;     // artboards folded away
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
    std::optional<ImRect> dropInto;   // the artboard a drag would land in: outlined
    std::optional<ImRect> stackArea;  // a stack's rows, where a link dropped goes into the stack
    std::optional<StackPlace> origin; // the one row a drag carries: where it was (a drop there changes nothing)
    // Links into slots that show no port (a folded artboard's layers): where they end. Nothing to grab there.
    struct LinkEnd { InputSlot slot; ImVec2 pos; };
    std::vector<LinkEnd> ends;

    const NodeItemPort* output() const;
    const NodeItemPort* input(const InputSlot& slot) const;
    std::optional<ImVec2> linkEnd(const InputSlot& slot) const;   // where a link into the slot ends: its port, or a LinkEnd
    void moveTo(float x, float y);   // the node moved: same layout, shifted
};


// Lays `n` out into `out`, reusing its storage.
void buildNodeItem(const Project& project, const Node& n, const NodeItemState& view, StackLayout stack, NodeItem& out);

// A layer row's parts, inside its rect: its eye, its thumbnail.
ImRect layerEyeRect(const ImRect& row);
ImRect layerThumbRect(const ImRect& row);
// An artboard row's: its triangle, its eye, its colour.
ImRect artboardFoldRect(const ImRect& row);
ImRect artboardEyeRect(const ImRect& row);
ImRect artboardColorRect(const ImRect& row);

#pragma once
// Where a link being dragged lands on a node: the rules, apart from the panel that asks.
//   from an output     over a stack's rows (a little past them): into the stack, at a place, as rows
//                      are dropped; on an input's port: that input; over a channel's row: that
//                      channel's mask; on a one-input node's body: its input; elsewhere: nothing
//   from an input      on an output's port: that output
// Graph space throughout.
//
//   const LinkLanding l = linkLanding(item, node, at, port, end, fits, place);

#include "ui/panels/graph/internal/node-item.hpp"

#include <functional>
#include <vector>

// The zones a link lands in on an item: its stack's rows (a little past them), a channel's row (its
// slider, its value), its first box (title, picture).
struct LinkZone {
    enum class Kind : uint8_t { None, Stack, Channel, Body };
    Kind kind = Kind::None;
    const NodeItemChannelRow* channel = nullptr;   // Channel's
};
LinkZone linkZoneAt(const NodeItem& item, ImVec2 graphPoint);

// The link's end being dragged.
struct LinkEnd {
    bool   fromOutput = true;            // its other end is an output: looking for an input
    NodeId liftedSource = kNoNodeId;     // lifted off an input (link.move): the link's source ...
    NodeId liftedFrom = kNoNodeId;       // ... and the node it came off
};

struct LinkLanding {
    bool      valid = false;
    InputSlot slot;          // where on the node (from an input: none, the node's output)
    StackGap  place;         // into a stack: where (kind Link), else none
    ImVec2    pos;           // where the link's end shows
};

// `fits(slot)`: the workspace takes the link there (from an input: its output takes it, slot unused).
// `place(at, carried)`: the drop on the item's stack at `at`, carrying `carried` (stackPlace).
LinkLanding linkLanding(const NodeItem& item, const Node& node, ImVec2 at, const NodeItemPort* port, const LinkEnd& end,
                        const std::function<bool(const InputSlot&)>& fits,
                        const std::function<StackGap(ImVec2, const std::vector<StackRowRef>&)>& place);

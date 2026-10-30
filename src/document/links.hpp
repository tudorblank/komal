#pragma once
// What feeds a node, and the rules for linking: every edit that links, unlinks, heals or restores
// links goes through here, and so does every reader that walks a node's links. What can feed a
// node is its kind's InputShape (node-kinds.hpp): one input, a stack, or nothing; any node with a
// signal can take masks. Wrapped in a snapshot, an edit tells exactly what it changed:
//
//   const NodeInputs before = NodeInputs::of(node);
//   Links::connect(key, source, node, slot);                           // canConnect() said yes
//   Links::emitChanges(id, before, NodeInputs::of(node), emit);        // LinkAdded / LinkRemoved / LayersChanged

#include "document/input-slot.hpp"
#include "document/project-event.hpp"

#include <absl/container/flat_hash_set.h>
#include <array>
#include <utility>
#include <vector>

// What feeds a node: its input (InputShape::One), its stack, bottom first (Stack), and its masks.
struct NodeInputs {
    NodePtr input;
    std::vector<NodePtr> layers;
    std::array<std::array<NodePtr, kChannelCount>, kSignalCount> masks;

    static NodeInputs of(const Node& n);
    bool operator==(const NodeInputs&) const = default;
};

namespace Links {

NodePtr inputAt(const Node& n, const InputSlot& slot);   // what feeds that slot, if anything
// Every link into `n`: its source and the slot it lands on, the stack bottom first.
std::vector<std::pair<NodePtr, InputSlot>> into(const Node& n);
// The slot a link dropped on the node's body connects to: its input, or a new top layer (none: false).
bool preferredInput(const Node& n, InputSlot& out);

// Whether `source` may feed `slot` of `node`: never a cycle, a node once per stack (dropping it
// on its own slot again is fine), a Dry / Wet mask only where the node has that signal.
bool canConnect(const NodePtr& source, const Node& node, const InputSlot& slot);
// Where canConnect() said yes. A layer dropped at another position in its own stack moves there.
void connect(GraphKey key, const NodePtr& source, Node& node, const InputSlot& slot);
void disconnect(GraphKey key, Node& node, const InputSlot& slot);   // where inputAt() found something
// `node` fed exactly `want` (its shape's part of it): what it held is let go of first, so the
// graph never passes through a cycle when a whole graph is restored node by node.
void restore(GraphKey key, Node& node, const NodeInputs& want);

// Every slot of `node` a `deleted` node fed, fed again from upstream: through deleted nodes with
// exactly one input each (a slot nothing upstream can feed keeps its link until the node goes).
void heal(GraphKey key, Node& node, const absl::flat_hash_set<NodeId>& deleted);

// The events that take node `to` from `before` to `now`: per single input (input, masks) the link
// removed, then the one added; the stack once, as LayersChanged.
void emitChanges(NodeId to, const NodeInputs& before, const NodeInputs& now, const EmitEvent& emit);

}

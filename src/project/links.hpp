#pragma once
// The Workspace's rules for links, on top of the nodes' own (Node::canLink / link / unlink, which
// know each port): where a link dropped on a node's body goes, whether a lifted link may land
// somewhere, how deleting heals the graph, and the events that tell a node's links changed.
//
//   const NodeLinks before = node.links();
//   node.link(key, source, slot);                                       // canLink() said yes
//   Links::emitChanges(id, before, node.links(), emit);                 // LinkAdded / LinkRemoved / LayersChanged

#include "project/workspace-event.hpp"
#include "node/node.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <optional>
#include <vector>

namespace Links {

// The slot a link dropped on the node's body connects to: its input, or a new top layer (none: false).
bool preferredInput(const Node& n, InputSlot& out);

// Whether the link feeding `slot` of `from` (from `source`), lifted off, may land on `toSlot` of
// `to` (null: off, fine whenever it's there): judged on the graph without it. In its own stack a
// lifted layer lands at a position (NewLayer), never on another layer's row; dropped back where it
// was is fine.
bool canMove(const NodePtr& source, const Node& from, const InputSlot& slot, const Node* to, const InputSlot& toSlot);

// Where `layer` is in a stack's layers (none: not there).
std::optional<size_t> layerIndex(const Node& stack, NodeId layer);

// Every slot of `node` a `deleted` node fed, fed again from upstream: through deleted nodes with
// exactly one input each (a slot nothing upstream can feed keeps its link until the node goes).
void heal(GraphKey key, Node& node, const absl::flat_hash_set<NodeId>& deleted);

// The events that take node `to` from `before` to `now`: per single input (input, masks) the link
// removed, then the one added; the stack once, as LayersChanged.
void emitChanges(NodeId to, const NodeLinks& before, const NodeLinks& now, const EmitEvent& emit);

// Every node's links before a bulk edit (nodes deleted or brought back at once), to tell what
// changed once it's done.
//
//   Links::Before before(registry.nodes());
//   ...                              // the edit
//   before.tellGone(id, emit);       // a node that went
//   before.tellNow(node, emit);      // a node that's there
class Before {
public:
    explicit Before(const std::vector<NodePtr>& nodes);
    void cameBack(NodeId id) { m_links.emplace(id, NodeLinks{}); }   // linked to nothing before
    void tellGone(NodeId id, const EmitEvent& emit);   // its links off (its stack goes with it, untold)
    void tellNow(const Node& n, const EmitEvent& emit) const;

private:
    absl::flat_hash_map<NodeId, NodeLinks> m_links;
};

}

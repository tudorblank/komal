#pragma once
// The graph as a history step leaves it: every node, its settings and its links, and the master's
// artboards. Undo and redo restore it; pixels travel separately, as raster commits. A kind's own
// settings are its NodeSettings: a new kind needs nothing here.
//
// A node's state is immutable once captured, and steps share it: a step that moved one node of
// five hundred holds one new NodeState and the other 499 of the step before.
//
//   const GraphState s = GraphState::capture(nodes);
//   ...                                                     // nodes 4 and 7 change
//   const GraphState t = GraphState::capture(nodes, &s, { 4, 7 });   // the rest shared with s
//   s.restore(key, nodes, emit);   // nodes come back or go, links and settings as they were

#include "document/artboard.hpp"
#include "document/links.hpp"
#include "document/node-registry.hpp"

#include <absl/container/flat_hash_set.h>
#include <memory>
#include <string>
#include <vector>

struct NodeState {
    NodePtr      node;   // the node itself: history keeps a deleted node alive, so it comes back as itself
    std::string  label;
    float        x = 0.0f, y = 0.0f;
    float        scale[kSignalCount][kChannelCount] = {};
    NodeId       mask[kSignalCount][kChannelCount] = {};
    NodeId       input = kNoNodeId;    // what feeds it, by its kind's shape: one input...
    std::vector<NodeId> layers;        // ... or a stack, bottom first
    NodeSettings settings;
    bool operator==(const NodeState&) const = default;
};
using NodeStatePtr = std::shared_ptr<const NodeState>;

struct GraphState {
    std::vector<NodeStatePtr> nodes;   // creation order
    std::shared_ptr<const Artboards> artboards;   // shared with the step before while they're the same

    static GraphState capture(const NodeRegistry& registry, const Artboards& artboards);   // every node anew
    // Every node anew but those `previous` holds and `changed` doesn't name: theirs are shared.
    // `previous` must be the graph as it was before exactly those nodes changed (Workspace tracks them).
    static GraphState capture(const NodeRegistry& registry, const Artboards& artboards, const GraphState& previous,
                              const absl::flat_hash_set<NodeId>& changed);
    // Makes the registry's graph this one (the artboards are the Workspace's to take). Nodes that come back do so as themselves, told right
    // away (unlinked); links, settings and the nodes that go are told once it's all done. Links
    // are all let go of before any is taken, so the graph never passes through a cycle on the way.
    void restore(GraphKey key, NodeRegistry& registry, const EmitEvent& emit) const;

    bool operator==(const GraphState& o) const;   // node for node: the same state, shared or equal
};

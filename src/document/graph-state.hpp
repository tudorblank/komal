#pragma once
// The graph as a history step leaves it: every node, its settings and its links. Undo and redo
// restore it; pixels travel separately, as raster commits. A kind's own settings are its
// NodeSettings: a new kind needs nothing here.
//
//   const GraphState s = GraphState::capture(nodes);
//   ...                       // anything changes
//   s.restore(key, nodes, emit);   // nodes come back or go, links and settings as they were

#include "document/links.hpp"
#include "document/node-registry.hpp"

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

struct GraphState {
    std::vector<NodeState> nodes;   // creation order

    static GraphState capture(const NodeRegistry& registry);
    // Makes the registry's graph this one. Nodes that come back do so as themselves, told right
    // away (unlinked); links, settings and the nodes that go are told once it's all done. Links
    // are all let go of before any is taken, so the graph never passes through a cycle on the way.
    void restore(GraphKey key, NodeRegistry& registry, const EmitEvent& emit) const;

    bool operator==(const GraphState&) const = default;
};

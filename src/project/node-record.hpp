#pragma once
// A node's state as plain values: its name, place, signal scales, settings and links (by id).
// History keeps one per node (NodeState), a file one per node (WorkspaceFile::SavedNode): a new
// per-node field is added here, once.

#include "node/node.hpp"

#include <string>
#include <vector>

struct NodeRecord {
    std::string  label;
    float        x = 0.0f, y = 0.0f;
    float        scale[kSignalCount][kChannelCount] = {};
    NodeId       mask[kSignalCount][kChannelCount] = {};
    NodeId       input = kNoNodeId;    // what feeds it, by its kind's shape: one input...
    std::vector<NodeId> layers;        // ... or a stack, bottom first
    NodeSettings settings;

    static NodeRecord of(const Node& n);   // the node as it is now
    bool operator==(const NodeRecord&) const = default;
};

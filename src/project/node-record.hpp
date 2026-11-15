#pragma once
// A node's state as plain values: its name, place, signal scales, settings and links (by id).
// History keeps one per node (NodeState), a file one per node (WorkspaceFile::SavedNode): a new
// per-node field is read here (of) and written back here (applyTo), once.

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

    // Its own fields back onto `n`, those that differ: place, name, scales, settings. Not its
    // links: history sets them whole, a file checks each one as it's made. After the links (a
    // stack's settings name its layers).
    struct Applied { bool moved = false, changed = false; };   // place / anything else
    Applied applyTo(GraphKey key, Node& n) const;

    bool operator==(const NodeRecord&) const = default;
};

#pragma once
// A stack's rows moved together, the way the graph moves them: several layers (from anywhere in
// the stack) and whole groups (the master's artboards, with their layers), dropped at one place.
// They keep their order and close up: no gaps where they were.
//
//   [1 2 3 4] (bottom first), 1 and 3 moved to the top: [2 4 1 3]; moved above 2: [2 1 3 4]
//
// A place is a position among what's left at the top level, or inside a group (layers only:
// groups don't nest). The top level is StackGroups::entries: the same the graph lays out.

#include "node/node.hpp"

#include <optional>
#include <vector>

struct StackRows {
    std::vector<NodeId>     layers;   // bottom first
    std::vector<StackGroup> groups;   // in stack order, none overlapping
    bool operator==(const StackRows&) const = default;
};

struct StackMove {
    std::vector<NodeId>   layers;   // layers moved, wherever they are ...
    std::vector<GroupKey> groups;   // ... and whole groups, every layer in them along
    GroupKey into = kNoGroup;       // where to: inside this group (none: the top level) ...
    size_t   at = 0;                // ... at this position among the rows left there (0 = bottom)
};

// The rows after the move; nullopt when it doesn't fit (a layer or group that isn't there, a group
// into a group, a place out of range).
std::optional<StackRows> moveRows(const StackRows& now, const StackMove& move);

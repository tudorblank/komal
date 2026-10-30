#pragma once
// Which nodes are selected, and the rules that make the next selection:
//   click         only this one                                     only(id)
//   Ctrl+click    add or remove it                                  toggled(id)
//   Shift+click   the shortest link path from the anchor (the last   chained(project, id, additive)
//                 clicked); Ctrl+Shift adds it
//   rubber band   what it touches (the graph picks the ids)         withIds(ids)
// A value: the project keeps the one that counts (Project::selection(), changed by the
// selection.set action); a view works out the next one with these rules and runs the action.

#include "node/node.hpp"

#include <absl/container/flat_hash_set.h>
#include <vector>

class Project;

class NodeSelection {
public:
    using Set = absl::flat_hash_set<NodeId>;

    NodeSelection() = default;
    NodeSelection(Set ids, NodeId anchor) : m_ids(std::move(ids)), m_anchor(anchor) {}

    const Set& ids() const { return m_ids; }
    bool   contains(NodeId id) const { return m_ids.contains(id); }
    bool   empty() const { return m_ids.empty(); }
    size_t size() const { return m_ids.size(); }
    NodeId anchor() const { return m_anchor; }
    NodeId single() const { return m_ids.size() == 1 ? *m_ids.begin() : kNoNodeId; }
    std::vector<NodeId> list() const { return { m_ids.begin(), m_ids.end() }; }

    static NodeSelection only(NodeId id) { return { Set{ id }, id }; }
    NodeSelection toggled(NodeId id) const;
    NodeSelection chained(const Project& project, NodeId id, bool additive) const;   // the anchor stays
    NodeSelection withIds(Set ids) const { return { std::move(ids), m_anchor }; }
    NodeSelection withAnchor(NodeId id) const { return { m_ids, id }; }
    NodeSelection without(NodeId id) const;   // a node that went

    bool operator==(const NodeSelection&) const = default;

private:
    Set    m_ids;
    NodeId m_anchor = kNoNodeId;
};

// Shortest path between two nodes over links in either direction (inputs, layers and masks),
// both ends included; empty when they aren't connected.
std::vector<NodeId> linkPath(const Project& project, NodeId from, NodeId to);

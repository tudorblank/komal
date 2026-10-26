#pragma once
// The graph's selection and its rules:
//   click         select only this node
//   Ctrl+click    add or remove it
//   Shift+click   the shortest link path from the anchor (last clicked); Ctrl+Shift adds it
//   rubber band   select what it touches; Ctrl toggles, Shift adds

#include "node/node.hpp"

#include <absl/container/flat_hash_set.h>
#include <vector>

class Project;

class GraphSelection {
public:
    using Set = absl::flat_hash_set<NodeId>;

    const Set& ids() const { return m_ids; }
    bool   contains(NodeId id) const { return m_ids.contains(id); }
    bool   empty() const { return m_ids.empty(); }
    size_t size() const { return m_ids.size(); }
    NodeId anchor() const { return m_anchor; }
    NodeId single() const { return m_ids.size() == 1 ? *m_ids.begin() : kNoNodeId; }

    void clear() { m_ids.clear(); }
    void select(NodeId id) { m_ids.clear(); m_ids.insert(id); m_anchor = id; }
    void toggle(NodeId id);
    void setAnchor(NodeId id) { m_anchor = id; }
    void assign(Set ids) { m_ids = std::move(ids); }
    void erase(NodeId id);
    // Shift+click: the chain from the anchor to `id` (just `id` when they aren't linked).
    void chain(const Project& project, NodeId id, bool additive);

private:
    Set    m_ids;
    NodeId m_anchor = kNoNodeId;
};

// Shortest path between two nodes over links in either direction (inputs, layers and masks),
// both ends included; empty when they aren't connected.
std::vector<NodeId> linkPath(const Project& project, NodeId from, NodeId to);

#include "document/node-selection.hpp"
#include "document/project.hpp"

#include <absl/container/flat_hash_map.h>
#include <algorithm>
#include <deque>

NodeSelection NodeSelection::toggled(NodeId id) const
{
    Set next = m_ids;
    if (!next.erase(id)) next.insert(id);
    return { std::move(next), id };
}

NodeSelection NodeSelection::chained(const Project& project, NodeId id, bool additive) const
{
    std::vector<NodeId> path = m_anchor != kNoNodeId ? linkPath(project, m_anchor, id) : std::vector<NodeId>{};
    if (path.empty()) path.push_back(id);
    Set next = additive ? m_ids : Set{};
    next.insert(path.begin(), path.end());   // the anchor stays: the next Shift+click chains from it too
    return { std::move(next), m_anchor };
}

NodeSelection NodeSelection::without(NodeId id) const
{
    Set next = m_ids;
    next.erase(id);
    return { std::move(next), m_anchor == id ? kNoNodeId : m_anchor };
}

std::vector<NodeId> linkPath(const Project& project, NodeId from, NodeId to)
{
    if (!project.find(from) || !project.find(to)) return {};

    // Undirected adjacency over every link.
    absl::flat_hash_map<NodeId, std::vector<NodeId>> adj;
    for (const NodePtr& n : project.nodes())
        for (const NodePtr& d : n->allDeps()) {
            adj[n->id()].push_back(d->id());
            adj[d->id()].push_back(n->id());
        }

    absl::flat_hash_map<NodeId, NodeId> parent{ { from, kNoNodeId } };
    std::deque<NodeId> queue{ from };
    while (!queue.empty() && !parent.contains(to)) {
        const NodeId cur = queue.front();
        queue.pop_front();
        for (NodeId next : adj[cur])
            if (parent.try_emplace(next, cur).second) queue.push_back(next);
    }
    if (!parent.contains(to)) return {};

    std::vector<NodeId> path;
    for (NodeId cur = to; cur != kNoNodeId; cur = parent[cur]) path.push_back(cur);
    std::reverse(path.begin(), path.end());
    return path;
}

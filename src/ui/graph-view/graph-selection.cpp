#include "graph-selection.h"
#include "core/project.h"

#include <absl/container/flat_hash_map.h>
#include <algorithm>
#include <deque>

void GraphSelection::toggle(NodeId id)
{
    if (!m_ids.erase(id)) m_ids.insert(id);
    m_anchor = id;
}

void GraphSelection::erase(NodeId id)
{
    m_ids.erase(id);
    if (m_anchor == id) m_anchor = kNoNodeId;
}

void GraphSelection::chain(const Project& project, NodeId id, bool additive)
{
    std::vector<NodeId> path = m_anchor != kNoNodeId ? linkPath(project, m_anchor, id) : std::vector<NodeId>{};
    if (path.empty()) path.push_back(id);
    if (!additive) m_ids.clear();
    m_ids.insert(path.begin(), path.end());   // the anchor stays: the next Shift+click chains from it too
}

std::vector<NodeId> linkPath(const Project& project, NodeId from, NodeId to)
{
    if (!project.find(from) || !project.find(to)) return {};

    // Undirected adjacency over every link.
    absl::flat_hash_map<NodeId, std::vector<NodeId>> adj;
    for (const NodePtr& n : project.nodes())
        for (const NodePtr& d : n->allDeps()) {
            adj[n->m_meta.id].push_back(d->m_meta.id);
            adj[d->m_meta.id].push_back(n->m_meta.id);
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

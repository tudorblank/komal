#include "graph/selection.hpp"
#include "core/project.hpp"

#include <absl/container/flat_hash_map.h>
#include <algorithm>
#include <deque>

void GraphSelection::select(NodeId id)
{
    m_anchor = id;
    if (m_ids.size() == 1 && m_ids.contains(id)) return;
    m_ids.clear();
    m_ids.insert(id);
    changed();
}

void GraphSelection::toggle(NodeId id)
{
    if (!m_ids.erase(id)) m_ids.insert(id);
    m_anchor = id;
    changed();
}

void GraphSelection::assign(Set ids)
{
    if (ids == m_ids) return;   // a rubber band assigns every frame it's dragged
    m_ids = std::move(ids);
    changed();
}

void GraphSelection::erase(NodeId id)
{
    if (m_anchor == id) m_anchor = kNoNodeId;
    if (m_ids.erase(id)) changed();
}

void GraphSelection::chain(const Project& project, NodeId id, bool additive)
{
    std::vector<NodeId> path = m_anchor != kNoNodeId ? linkPath(project, m_anchor, id) : std::vector<NodeId>{};
    if (path.empty()) path.push_back(id);
    Set next = additive ? m_ids : Set{};
    next.insert(path.begin(), path.end());   // the anchor stays: the next Shift+click chains from it too
    assign(std::move(next));
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

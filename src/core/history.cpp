#include "history.h"

#include <cassert>

bool History::push(HistoryStep step)
{
    if (step.empty()) return false;

    const uint32_t id = uint32_t(m_nodes.size());
    Node& n = m_nodes.emplace_back();
    n.parent = m_current;
    n.depth  = m_nodes[m_current].depth + 1;
    n.step   = std::move(step);

    m_nodes[m_current].children.push_back(id);
    m_nodes[m_current].redoChild = id;
    m_current = id;
    return true;
}

void History::goTo(uint32_t target, const FlipRaster& flip)
{
    assert(target < m_nodes.size());
    std::vector<uint32_t> down;   // target side, leaf first
    uint32_t a = m_current, b = target;

    const auto up = [&] {
        apply(a, false, flip);
        const uint32_t p = m_nodes[a].parent;
        m_nodes[p].redoChild = a;   // redo comes back the way we left
        a = p;
    };
    while (m_nodes[a].depth > m_nodes[b].depth) up();
    while (m_nodes[b].depth > m_nodes[a].depth) { down.push_back(b); b = m_nodes[b].parent; }
    while (a != b) { up(); down.push_back(b); b = m_nodes[b].parent; }
    for (auto it = down.rbegin(); it != down.rend(); ++it) apply(*it, true, flip);

    m_current = target;
}

bool History::undo(const FlipRaster& flip)
{
    if (!canUndo()) return false;
    goTo(m_nodes[m_current].parent, flip);
    return true;
}

bool History::redo(const FlipRaster& flip)
{
    if (!canRedo()) return false;
    goTo(m_nodes[m_current].redoChild, flip);
    return true;
}

void History::apply(uint32_t id, bool forward, const FlipRaster& flip)
{
    // Commits of one step on the same raster must come undone in the opposite order.
    const auto& commits = m_nodes[id].step.rasterCommits;
    if (forward) for (auto it = commits.begin(); it != commits.end(); ++it) flip(*it);
    else         for (auto it = commits.rbegin(); it != commits.rend(); ++it) flip(*it);
}

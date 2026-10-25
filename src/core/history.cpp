#include "history.h"

#include <algorithm>
#include <cassert>

bool History::push(RasterSteps steps)
{
    std::erase_if(steps, [](const auto& s) { return s.second.empty(); });
    if (steps.empty()) return false;

    const uint32_t id = uint32_t(m_nodes.size());
    Node& n = m_nodes.emplace_back();
    n.parent = m_current;
    n.depth  = m_nodes[m_current].depth + 1;
    n.steps  = std::move(steps);
    for (const auto& [rid, d] : n.steps) n.bytes += d.bytes();

    m_nodes[m_current].children.push_back(id);
    m_nodes[m_current].redoChild = id;
    m_current = id;
    return true;
}

void History::goTo(uint32_t target, const ApplyFn& apply)
{
    assert(target < m_nodes.size());
    std::vector<uint32_t> down;   // target side, leaf first
    uint32_t a = m_current, b = target;

    const auto up = [&] {
        flip(a, apply);
        const uint32_t p = m_nodes[a].parent;
        m_nodes[p].redoChild = a;   // redo comes back the way we left
        a = p;
    };
    while (m_nodes[a].depth > m_nodes[b].depth) up();
    while (m_nodes[b].depth > m_nodes[a].depth) { down.push_back(b); b = m_nodes[b].parent; }
    while (a != b) { up(); down.push_back(b); b = m_nodes[b].parent; }
    for (auto it = down.rbegin(); it != down.rend(); ++it) flip(*it, apply);

    m_current = target;
}

bool History::undo(const ApplyFn& apply)
{
    if (!canUndo()) return false;
    goTo(m_nodes[m_current].parent, apply);
    return true;
}

bool History::redo(const ApplyFn& apply)
{
    if (!canRedo()) return false;
    goTo(m_nodes[m_current].redoChild, apply);
    return true;
}

size_t History::bytes() const
{
    size_t n = 0;
    for (const Node& node : m_nodes) n += node.bytes;
    return n;
}

void History::flip(uint32_t id, const ApplyFn& apply)
{
    Node& n = m_nodes[id];
    n.bytes = 0;
    for (auto& [rid, delta] : n.steps) {
        apply(rid, delta);
        n.bytes += delta.bytes();   // after the swap it holds the other side
    }
}

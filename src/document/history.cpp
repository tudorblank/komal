#include "history.hpp"

#include <cassert>

void History::push(HistoryStep step)
{
    const StepId id = StepId(m_nodes.size());
    const uint32_t depth = at(m_current).depth + 1;
    Node& n = m_nodes.emplace_back();
    n.parent = m_current;
    n.depth  = depth;
    n.step   = std::move(step);

    at(m_current).children.push_back(id);
    at(m_current).redoChild = id;
    m_current = id;
}

void History::goTo(StepId target, const FlipRaster& flip)
{
    assert(contains(target));
    std::vector<StepId> down;   // target side, leaf first
    StepId a = m_current, b = target;

    const auto up = [&] {
        apply(a, false, flip);
        const StepId p = at(a).parent;
        at(p).redoChild = a;   // redo comes back the way we left
        a = p;
    };
    while (at(a).depth > at(b).depth) up();
    while (at(b).depth > at(a).depth) { down.push_back(b); b = at(b).parent; }
    while (a != b) { up(); down.push_back(b); b = at(b).parent; }
    for (auto it = down.rbegin(); it != down.rend(); ++it) apply(*it, true, flip);

    m_current = target;
}

void History::apply(StepId id, bool forward, const FlipRaster& flip)
{
    // Commits of one step on the same raster must come undone in the opposite order.
    const auto& commits = at(id).step.rasterCommits;
    if (forward) for (auto it = commits.begin(); it != commits.end(); ++it) flip(*it);
    else         for (auto it = commits.rbegin(); it != commits.rend(); ++it) flip(*it);
}

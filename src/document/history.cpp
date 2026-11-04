#include "history.hpp"

#include <algorithm>
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

void History::goTo(StepId target)
{
    assert(contains(target));
    StepId a = m_current, b = target;
    const auto up = [&] {
        const StepId p = at(a).parent;
        at(p).redoChild = a;   // redo comes back the way we left
        a = p;
    };
    while (at(a).depth > at(b).depth) up();
    while (at(b).depth > at(a).depth) b = at(b).parent;
    while (a != b) { up(); b = at(b).parent; }
    m_current = target;
}

void History::remove(const std::vector<StepId>& steps)
{
    std::vector<uint8_t> gone(m_nodes.size(), 0);
    for (StepId s : steps)
        if (contains(s) && s != kStartStep) gone[size_t(s)] = 1;
    assert(!gone[size_t(m_current)] && "the current step goes first");
    if (std::none_of(gone.begin(), gone.end(), [](uint8_t g) { return g != 0; })) return;

    // A step's children as they'll be: a removed child's own children in its place, all the way down.
    const auto childrenLeft = [&](StepId id) {
        std::vector<StepId> out;
        std::vector<StepId> todo(at(id).children.rbegin(), at(id).children.rend());
        while (!todo.empty()) {
            const StepId c = todo.back();
            todo.pop_back();
            if (!gone[size_t(c)]) { out.push_back(c); continue; }
            todo.insert(todo.end(), at(c).children.rbegin(), at(c).children.rend());
        }
        return out;
    };
    // Redo's way on: through removed steps to the first one left.
    const auto redoLeft = [&](StepId id) {
        StepId r = at(id).redoChild;
        while (r != kNoStep && gone[size_t(r)]) r = at(r).redoChild;
        return r;
    };

    // Numbered again, in the order they were made.
    std::vector<StepId> renumber(m_nodes.size(), kNoStep);
    uint32_t next = 0;
    for (size_t i = 0; i < m_nodes.size(); ++i)
        if (!gone[i]) renumber[i] = StepId(next++);
    const auto newId = [&](StepId old) { return old == kNoStep ? kNoStep : renumber[size_t(old)]; };

    std::vector<Node> kept;
    kept.reserve(next);
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (gone[i]) continue;
        const StepId id = StepId(i);
        Node n;
        n.step = std::move(at(id).step);
        for (StepId c : childrenLeft(id)) n.children.push_back(newId(c));
        const StepId redo = redoLeft(id);
        n.redoChild = redo != kNoStep ? newId(redo) : (n.children.empty() ? kNoStep : n.children.back());
        kept.push_back(std::move(n));
    }
    for (size_t i = 0; i < kept.size(); ++i)   // parents and depths from the children, top down (parents come first)
        for (StepId c : kept[i].children) {
            kept[size_t(c)].parent = StepId(i);
            kept[size_t(c)].depth  = kept[i].depth + 1;
        }
    m_current = newId(m_current);
    m_nodes = std::move(kept);
}

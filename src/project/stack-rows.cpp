#include "project/stack-rows.hpp"

#include <algorithm>

namespace {

// One row at the top level: a layer of its own, or a group with its layers (bottom first).
struct Entry {
    GroupKey group = kNoGroup;   // none: a layer
    std::vector<NodeId> layers;
};

template <class T> bool has(const std::vector<T>& v, const T& x) { return std::find(v.begin(), v.end(), x) != v.end(); }

bool hasGroup(const std::vector<StackGroup>& groups, GroupKey key)
{
    return std::any_of(groups.begin(), groups.end(), [&](const StackGroup& g) { return g.key == key; });
}

// The top level split: what `move` takes out, in stack order (a group whole; a layer from wherever
// it is: a group moving takes its layers along), and what's left (a group keeps its place, with
// the layers it keeps).
struct Split { std::vector<Entry> moved, left; };
Split split(const StackRows& now, const StackMove& move)
{
    Split out;
    for (const StackGroups::Entry& g : StackGroups::entries(now.groups, now.layers.size())) {
        Entry e{ g.group, { now.layers.begin() + g.begin, now.layers.begin() + g.end } };
        if (e.group != kNoGroup && has(move.groups, e.group)) { out.moved.push_back(std::move(e)); continue; }
        if (e.group == kNoGroup) {
            (has(move.layers, e.layers[0]) ? out.moved : out.left).push_back(std::move(e));
            continue;
        }
        Entry kept{ e.group, {} };
        for (NodeId id : e.layers) {
            if (has(move.layers, id)) out.moved.push_back({ kNoGroup, { id } });
            else kept.layers.push_back(id);
        }
        out.left.push_back(std::move(kept));
    }
    return out;
}

}

StackRows stackRowsOf(const Node& stack)
{
    StackRows rows;
    for (const NodePtr& l : stack.links().layers) rows.layers.push_back(l->id());
    rows.groups = stackSettings(stack).groups;
    return rows;
}

size_t rowsLeft(const StackRows& now, const StackMove& carried, GroupKey into)
{
    const Split s = split(now, carried);
    if (into == kNoGroup) return s.left.size();
    for (const Entry& e : s.left)
        if (e.group == into) return e.layers.size();
    return 0;
}

std::optional<StackRows> moveRows(const StackRows& now, const StackMove& move)
{
    for (NodeId id : move.layers)
        if (!has(now.layers, id)) return std::nullopt;
    for (GroupKey key : move.groups)
        if (key == kNoGroup || !hasGroup(now.groups, key)) return std::nullopt;
    if (move.into != kNoGroup && (!move.groups.empty() || !hasGroup(now.groups, move.into))) return std::nullopt;   // groups don't nest

    auto [moved, left] = split(now, move);
    if (moved.empty()) return std::nullopt;

    // Put them back at the place.
    if (move.into == kNoGroup) {
        if (move.at > left.size()) return std::nullopt;
        left.insert(left.begin() + std::ptrdiff_t(move.at), std::make_move_iterator(moved.begin()), std::make_move_iterator(moved.end()));
    } else {
        const auto into = std::find_if(left.begin(), left.end(), [&](const Entry& e) { return e.group == move.into; });
        if (into == left.end() || move.at > into->layers.size()) return std::nullopt;
        std::vector<NodeId> ids;
        for (const Entry& e : moved) ids.insert(ids.end(), e.layers.begin(), e.layers.end());
        into->layers.insert(into->layers.begin() + std::ptrdiff_t(move.at), ids.begin(), ids.end());
    }

    StackRows out;
    for (const Entry& e : left) {
        const uint32_t begin = uint32_t(out.layers.size());
        out.layers.insert(out.layers.end(), e.layers.begin(), e.layers.end());
        if (e.group != kNoGroup) out.groups.push_back({ e.group, begin, uint32_t(out.layers.size()) });
    }
    return out;
}

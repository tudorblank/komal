#include "project/graph-state.hpp"

#include <utility>

namespace {

NodeStatePtr stateOf(const NodePtr& node)
{
    return std::make_shared<const NodeState>(NodeState{ node, NodeRecord::of(*node) });
}

}

GraphState GraphState::capture(const NodeRegistry& registry, const Artboards& artboards)
{
    GraphState g;
    g.nodes.reserve(registry.nodes().size());
    for (const NodePtr& n : registry.nodes()) g.nodes.push_back(stateOf(n));
    g.artboards = std::make_shared<const Artboards>(artboards);
    return g;
}

GraphState GraphState::capture(const NodeRegistry& registry, const Artboards& artboards, const GraphState& previous,
                               const absl::flat_hash_set<NodeId>& changed)
{
    absl::flat_hash_map<NodeId, const NodeStatePtr*> kept;
    kept.reserve(previous.nodes.size());
    for (const NodeStatePtr& s : previous.nodes)
        if (!changed.contains(s->node->id())) kept.emplace(s->node->id(), &s);
    GraphState g;
    g.nodes.reserve(registry.nodes().size());
    for (const NodePtr& n : registry.nodes()) {
        const auto it = kept.find(n->id());
        g.nodes.push_back(it != kept.end() ? *it->second : stateOf(n));
    }
    g.artboards = *previous.artboards == artboards ? previous.artboards : std::make_shared<const Artboards>(artboards);
    return g;
}

bool GraphState::operator==(const GraphState& o) const
{
    if (nodes.size() != o.nodes.size() || (artboards != o.artboards && *artboards != *o.artboards)) return false;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i] != o.nodes[i] && !(*nodes[i] == *o.nodes[i])) return false;
    return true;
}

void GraphState::restore(GraphKey key, NodeRegistry& registry, const EmitEvent& emit) const
{
    absl::flat_hash_map<NodeId, const NodeState*> wanted;
    for (const NodeStatePtr& s : nodes) wanted.emplace(s->node->id(), s.get());
    const auto nodeOf = [&](NodeId id) -> NodePtr {
        const auto it = wanted.find(id);
        return it == wanted.end() ? nullptr : it->second->node;
    };
    const auto inputsWanted = [&](const NodeRecord& s) {
        NodeLinks in;
        in.input = nodeOf(s.input);
        for (NodeId id : s.layers) in.layers.push_back(nodeOf(id));
        for (int sig = 0; sig < kSignalCount; ++sig)
            for (int ch = 0; ch < kChannelCount; ++ch) in.masks[sig][ch] = nodeOf(s.mask[sig][ch]);
        return in;
    };
    const auto tell = [&](WorkspaceEventType type, NodeId id) { emit({ type, id, kNoNodeId, {} }); };

    // What fed every node before, for the events at the end.
    Links::Before before(registry.nodes());

    // Nodes that come back, as themselves: unlinked (they were let go of when they went), told now.
    for (const NodeStatePtr& s : nodes) {
        const NodeId id = s->node->id();
        if (registry.contains(id)) continue;
        registry.restore(s->node);
        before.cameBack(id);
        tell(WorkspaceEventType::NodeAdded, id);
    }

    // Links: every node whose inputs differ lets go of all of them first, then takes the wanted
    // ones, so the graph never passes through a cycle on the way (it only ever holds wanted links).
    std::vector<std::pair<Node*, NodeLinks>> relink;
    for (const NodePtr& n : registry.nodes()) {
        const auto it = wanted.find(n->id());
        NodeLinks want = it != wanted.end() ? inputsWanted(it->second->record) : NodeLinks{};
        if (n->links() == want) continue;
        n->setLinks(key, NodeLinks{});
        relink.emplace_back(n.get(), std::move(want));
    }
    for (auto& [n, in] : relink) n->setLinks(key, in);

    // Each node's own fields (after the links: a stack's settings name its layers).
    std::vector<NodeId> moved, changed;
    for (const NodeStatePtr& state : nodes) {
        const NodeRecord::Applied a = state->record.applyTo(key, *state->node);
        if (a.moved)   moved.push_back(state->node->id());
        if (a.changed) changed.push_back(state->node->id());
    }

    // Nodes that go: unlinked above, and nothing uses them any more. History keeps them.
    std::vector<NodeId> removed;
    for (const NodePtr& n : registry.nodes())
        if (!wanted.contains(n->id())) removed.push_back(n->id());
    for (NodeId id : removed) registry.remove(id);

    // Told, once it's all done: links, settings, then the nodes that went.
    for (const NodeStatePtr& s : nodes) before.tellNow(*s->node, emit);
    for (NodeId id : removed) before.tellGone(id, emit);
    for (NodeId id : moved)   tell(WorkspaceEventType::NodeMoved, id);
    for (NodeId id : changed) tell(WorkspaceEventType::NodeChanged, id);
    for (NodeId id : removed) tell(WorkspaceEventType::NodeRemoved, id);
}

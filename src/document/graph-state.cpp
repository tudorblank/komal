#include "document/graph-state.hpp"

#include <utility>

namespace {

NodeStatePtr stateOf(const NodePtr& node)
{
    const Node& n = *node;
    auto s = std::make_shared<NodeState>();
    s->node  = node;
    s->label = n.label();
    s->x = n.x();
    s->y = n.y();
    const NodeLinks& in = n.links();
    for (int sig = 0; sig < kSignalCount; ++sig)
        for (int ch = 0; ch < kChannelCount; ++ch) {
            s->scale[sig][ch] = n.signal(NodeSignal(sig)).scale[ch];
            s->mask[sig][ch]  = in.masks[sig][ch] ? in.masks[sig][ch]->id() : kNoNodeId;
        }
    if (in.input) s->input = in.input->id();
    for (const NodePtr& l : in.layers) s->layers.push_back(l->id());
    s->settings = n.settings();
    return s;
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
    g.artboards = previous.artboards && *previous.artboards == artboards ? previous.artboards : std::make_shared<const Artboards>(artboards);
    return g;
}

bool GraphState::operator==(const GraphState& o) const
{
    const auto boards = [](const GraphState& s) { return s.artboards ? *s.artboards : Artboards{}; };
    if (nodes.size() != o.nodes.size() || (artboards != o.artboards && boards(*this) != boards(o))) return false;
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
    const auto inputsWanted = [&](const NodeState& s) {
        NodeLinks in;
        in.input = nodeOf(s.input);
        for (NodeId id : s.layers) in.layers.push_back(nodeOf(id));
        for (int sig = 0; sig < kSignalCount; ++sig)
            for (int ch = 0; ch < kChannelCount; ++ch) in.masks[sig][ch] = nodeOf(s.mask[sig][ch]);
        return in;
    };
    const auto tell = [&](WorkspaceEventType type, NodeId id) { emit({ type, id, kNoNodeId, {} }); };

    // What fed every node before, for the events at the end.
    absl::flat_hash_map<NodeId, NodeLinks> before;
    for (const NodePtr& n : registry.nodes()) before.emplace(n->id(), n->links());

    // Nodes that come back, as themselves: unlinked (they were let go of when they went), told now.
    for (const NodeStatePtr& s : nodes) {
        const NodeId id = s->node->id();
        if (registry.contains(id)) continue;
        registry.restore(s->node);
        before.emplace(id, NodeLinks{});
        tell(WorkspaceEventType::NodeAdded, id);
    }

    // Links: every node whose inputs differ lets go of all of them first, then takes the wanted
    // ones, so the graph never passes through a cycle on the way (it only ever holds wanted links).
    std::vector<std::pair<Node*, NodeLinks>> relink;
    for (const NodePtr& n : registry.nodes()) {
        const auto it = wanted.find(n->id());
        NodeLinks want = it != wanted.end() ? inputsWanted(*it->second) : NodeLinks{};
        if (n->links() == want) continue;
        n->setLinks(key, NodeLinks{});
        relink.emplace_back(n.get(), std::move(want));
    }
    for (auto& [n, in] : relink) n->setLinks(key, in);

    // Settings, each node type its own.
    std::vector<NodeId> moved, changed;
    for (const NodeStatePtr& state : nodes) {
        const NodeState& s = *state;
        Node& n = *s.node;
        if (n.x() != s.x || n.y() != s.y) {
            n.setPosition(key, s.x, s.y);
            moved.push_back(n.id());
        }
        bool c = false;
        if (n.label() != s.label) { n.setLabel(key, s.label); c = true; }
        for (int sig = 0; sig < kSignalCount; ++sig)
            for (int ch = 0; ch < kChannelCount; ++ch)
                if (n.hasSignal(NodeSignal(sig)) && n.signal(NodeSignal(sig)).scale[ch] != s.scale[sig][ch]) {
                    n.setScale(key, NodeSignal(sig), NodeChannel(ch), s.scale[sig][ch]);
                    c = true;
                }
        if (n.settings() != s.settings) {
            n.setSettings(key, s.settings);
            c = true;
        }
        if (c) changed.push_back(n.id());
    }

    // Nodes that go: unlinked above, and nothing uses them any more. History keeps them.
    std::vector<NodeId> removed;
    for (const NodePtr& n : registry.nodes())
        if (!wanted.contains(n->id())) removed.push_back(n->id());
    for (NodeId id : removed) registry.remove(id);

    // Told, once it's all done: links, settings, then the nodes that went.
    for (const NodeStatePtr& s : nodes) {
        const NodeId id = s->node->id();
        Links::emitChanges(id, before[id], s->node->links(), emit);
    }
    for (NodeId id : removed) {
        NodeLinks& was = before[id];
        was.layers.clear();   // a stack goes with its node
        Links::emitChanges(id, was, NodeLinks{}, emit);
    }
    for (NodeId id : moved)   tell(WorkspaceEventType::NodeMoved, id);
    for (NodeId id : changed) tell(WorkspaceEventType::NodeChanged, id);
    for (NodeId id : removed) tell(WorkspaceEventType::NodeRemoved, id);
}

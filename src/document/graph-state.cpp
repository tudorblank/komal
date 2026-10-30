#include "document/graph-state.hpp"

#include <utility>

GraphState GraphState::capture(const NodeRegistry& registry)
{
    GraphState g;
    g.nodes.reserve(registry.nodes().size());
    for (const NodePtr& n : registry.nodes()) {
        NodeState& s = g.nodes.emplace_back();
        s.node  = n;
        s.label = n->label();
        s.x = n->x();
        s.y = n->y();
        const NodeInputs in = NodeInputs::of(*n);
        for (int sig = 0; sig < kSignalCount; ++sig)
            for (int ch = 0; ch < kChannelCount; ++ch) {
                s.scale[sig][ch] = n->signal(NodeSignal(sig)).scale[ch];
                s.mask[sig][ch]  = in.masks[sig][ch] ? in.masks[sig][ch]->id() : kNoNodeId;
            }
        if (in.input) s.input = in.input->id();
        for (const NodePtr& l : in.layers) s.layers.push_back(l->id());
        s.settings = n->settings();
    }
    return g;
}

void GraphState::restore(GraphKey key, NodeRegistry& registry, const EmitEvent& emit) const
{
    absl::flat_hash_map<NodeId, const NodeState*> wanted;
    for (const NodeState& s : nodes) wanted.emplace(s.node->id(), &s);
    const auto nodeOf = [&](NodeId id) -> NodePtr {
        const auto it = wanted.find(id);
        return it == wanted.end() ? nullptr : it->second->node;
    };
    const auto inputsWanted = [&](const NodeState& s) {
        NodeInputs in;
        in.input = nodeOf(s.input);
        for (NodeId id : s.layers) in.layers.push_back(nodeOf(id));
        for (int sig = 0; sig < kSignalCount; ++sig)
            for (int ch = 0; ch < kChannelCount; ++ch) in.masks[sig][ch] = nodeOf(s.mask[sig][ch]);
        return in;
    };
    const auto tell = [&](ProjectEventType type, NodeId id) { emit({ type, id, kNoNodeId, {} }); };

    // What fed every node before, for the events at the end.
    absl::flat_hash_map<NodeId, NodeInputs> before;
    for (const NodePtr& n : registry.nodes()) before.emplace(n->id(), NodeInputs::of(*n));

    // Nodes that come back, as themselves: unlinked (they were let go of when they went), told now.
    for (const NodeState& s : nodes) {
        const NodeId id = s.node->id();
        if (registry.contains(id)) continue;
        registry.restore(s.node);
        before.emplace(id, NodeInputs{});
        tell(ProjectEventType::NodeAdded, id);
    }

    // Links: every node whose inputs differ lets go of all of them first, then takes the wanted
    // ones, so the graph never passes through a cycle on the way (it only ever holds wanted links).
    std::vector<std::pair<Node*, NodeInputs>> relink;
    for (const NodePtr& n : registry.nodes()) {
        const auto it = wanted.find(n->id());
        NodeInputs want = it != wanted.end() ? inputsWanted(*it->second) : NodeInputs{};
        if (NodeInputs::of(*n) == want) continue;
        n->detachAllInputs(key);
        n->unlinkAllMasks(key);
        relink.emplace_back(n.get(), std::move(want));
    }
    for (auto& [n, in] : relink) Links::restore(key, *n, in);

    // Settings, each node type its own.
    std::vector<NodeId> moved, changed;
    for (const NodeState& s : nodes) {
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
    for (const NodeState& s : nodes) {
        const NodeId id = s.node->id();
        Links::emitChanges(id, before[id], NodeInputs::of(*s.node), emit);
    }
    for (NodeId id : removed) {
        NodeInputs& was = before[id];
        was.layers.clear();   // a stack goes with its node
        Links::emitChanges(id, was, NodeInputs{}, emit);
    }
    for (NodeId id : moved)   tell(ProjectEventType::NodeMoved, id);
    for (NodeId id : changed) tell(ProjectEventType::NodeChanged, id);
    for (NodeId id : removed) tell(ProjectEventType::NodeRemoved, id);
}

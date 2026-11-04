#include "document/links.hpp"

namespace {

    // The one input a node passes through, if it has exactly one.
    NodePtr singleInput(const Node& n)
    {
        const std::span<const NodePtr> in = n.inputs();
        return in.size() == 1 ? in[0] : nullptr;
    }

    // What feeds what `old` fed once the `deleted` nodes go (null: nothing, or `old` stays).
    NodePtr bridge(const NodePtr& old, const absl::flat_hash_set<NodeId>& deleted)
    {
        if (!old || !deleted.contains(old->id())) return nullptr;
        NodePtr source = singleInput(*old);
        while (source && deleted.contains(source->id())) source = singleInput(*source);   // the graph is acyclic
        return source;
    }

}

namespace Links {

bool preferredInput(const Node& n, InputSlot& out)
{
    switch (n.ports().inputs) {
    case InputShape::One:   out = InputSlot::input(); return true;
    case InputShape::Stack: out = InputSlot::newLayer(uint32_t(n.links().layers.size())); return true;
    case InputShape::None:  break;
    }
    return false;
}

bool canMove(const NodePtr& source, const Node& from, const InputSlot& slot, const Node& to, const InputSlot& toSlot)
{
    if (!source || from.linkAt(slot) != source) return false;   // not linked there
    if (&to == &from && toSlot == slot) return true;             // back where it was
    if (&to == &from && slot.is<InputSlot::Layer>()) {           // its own stack, one layer short
        if (const auto* at = toSlot.as<InputSlot::NewLayer>()) return at->at < from.links().layers.size();
        if (toSlot.is<InputSlot::Layer>()) return false;
    }
    // Lifting it changes nothing else a link elsewhere depends on: it's downstream of `source`, so
    // no cycle check runs through it.
    return to.canLink(source, toSlot);
}

void heal(GraphKey key, Node& node, const absl::flat_hash_set<NodeId>& deleted)
{
    for (const auto& [old, slot] : node.links().all()) {   // a copy: healing edits the links
        // Rows keep their place (nothing goes while healing); a source in the stack already can't
        // take another row.
        if (const NodePtr src = bridge(old, deleted); src && node.canLink(src, slot)) node.link(key, src, slot);
    }
}

void emitChanges(NodeId to, const NodeLinks& before, const NodeLinks& now, const EmitEvent& emit)
{
    const auto single = [&](const NodePtr& was, const NodePtr& is, const InputSlot& slot) {
        if (was == is) return;
        if (was) emit({ WorkspaceEventType::LinkRemoved, to, was->id(), slot });
        if (is)  emit({ WorkspaceEventType::LinkAdded,   to, is->id(),  slot });
    };
    single(before.input, now.input, InputSlot::input());
    for (int s = 0; s < kSignalCount; ++s)
        for (int ch = 0; ch < kChannelCount; ++ch)
            single(before.masks[s][ch], now.masks[s][ch], InputSlot::mask(NodeSignal(s), NodeChannel(ch)));
    if (before.layers != now.layers) emit({ WorkspaceEventType::LayersChanged, to, kNoNodeId, {} });
}

}

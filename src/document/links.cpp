#include "document/links.hpp"
#include "node/node-kinds.hpp"

#include <algorithm>

namespace {

// A node of each shape, once its kind said so.
const UnaryNode&      one(const Node& n)   { return static_cast<const UnaryNode&>(n); }
UnaryNode&            one(Node& n)         { return static_cast<UnaryNode&>(n); }
const CompositorNode& stack(const Node& n) { return static_cast<const CompositorNode&>(n); }
CompositorNode&       stack(Node& n)       { return static_cast<CompositorNode&>(n); }

// The one input a node passes through, if it has exactly one.
NodePtr singleInput(const Node& n)
{
    switch (inputShape(n)) {
    case InputShape::One:   return one(n).input();
    case InputShape::Stack: return stack(n).layers().size() == 1 ? stack(n).layers()[0] : nullptr;
    case InputShape::None:  break;
    }
    return nullptr;
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

NodeInputs NodeInputs::of(const Node& n)
{
    NodeInputs in;
    if (inputShape(n) == InputShape::One)   in.input  = one(n).input();
    if (inputShape(n) == InputShape::Stack) in.layers = stack(n).layers();
    for (int s = 0; s < kSignalCount; ++s)
        for (int ch = 0; ch < kChannelCount; ++ch) in.masks[s][ch] = n.signal(NodeSignal(s)).mask[ch];
    return in;
}

namespace Links {

NodePtr inputAt(const Node& n, const InputSlot& slot)
{
    if (slot.is<InputSlot::Input>()) {
        if (inputShape(n) == InputShape::One) return one(n).input();
    } else if (const auto* layer = slot.as<InputSlot::Layer>()) {
        if (inputShape(n) == InputShape::Stack && layer->index < stack(n).layers().size()) return stack(n).layers()[layer->index];
    } else if (const auto* mask = slot.as<InputSlot::Mask>()) {
        return n.signal(mask->signal).mask[int(mask->channel)];
    }
    return nullptr;   // a new layer is a position, not an input
}

std::vector<std::pair<NodePtr, InputSlot>> into(const Node& n)
{
    std::vector<std::pair<NodePtr, InputSlot>> out;
    const NodeInputs in = NodeInputs::of(n);
    if (in.input) out.push_back({ in.input, InputSlot::input() });
    for (uint32_t i = 0; i < uint32_t(in.layers.size()); ++i) out.push_back({ in.layers[i], InputSlot::stackLayer(i) });
    for (int s = 0; s < kSignalCount; ++s)
        for (int ch = 0; ch < kChannelCount; ++ch)
            if (in.masks[s][ch]) out.push_back({ in.masks[s][ch], InputSlot::mask(NodeSignal(s), NodeChannel(ch)) });
    return out;
}

bool preferredInput(const Node& n, InputSlot& out)
{
    switch (inputShape(n)) {
    case InputShape::One:   out = InputSlot::input(); return true;
    case InputShape::Stack: out = InputSlot::newLayer(uint32_t(stack(n).layers().size())); return true;
    case InputShape::None:  break;
    }
    return false;
}

bool canConnect(const NodePtr& source, const Node& node, const InputSlot& slot)
{
    if (!source || !node.canDependOn(source)) return false;   // also rules out cycles
    const InputShape shape = inputShape(node);
    if (slot.is<InputSlot::Input>()) return shape == InputShape::One;
    if (const auto* layer = slot.as<InputSlot::Layer>()) {
        if (shape != InputShape::Stack || layer->index >= stack(node).layers().size()) return false;
        return !stack(node).has(source) || stack(node).layers()[layer->index] == source;
    }
    if (const auto* at = slot.as<InputSlot::NewLayer>())   // a layer already in the stack moves there instead
        return shape == InputShape::Stack && at->at <= stack(node).layers().size();
    if (const auto* mask = slot.as<InputSlot::Mask>()) return node.hasSignal(mask->signal);
    return false;
}

void connect(GraphKey key, const NodePtr& source, Node& node, const InputSlot& slot)
{
    if (slot.is<InputSlot::Input>()) {
        one(node).setInput(key, source);
    } else if (const auto* layer = slot.as<InputSlot::Layer>()) {
        CompositorNode& c = stack(node);
        NodePtr old = c.layers()[layer->index];   // copy: replaceInputInstance edits the stack
        c.replaceInputInstance(key, old, source);
    } else if (const auto* pos = slot.as<InputSlot::NewLayer>()) {
        CompositorNode& c = stack(node);
        const auto& layers = c.layers();
        if (const auto it = std::find(layers.begin(), layers.end(), source); it != layers.end()) {
            const size_t from = size_t(it - layers.begin());
            const size_t at = pos->at > from ? pos->at - 1 : pos->at;   // it leaves its own place first
            if (at != from) c.moveLayer(key, from, at);
        } else {
            c.addLayerAt(key, source, pos->at);
        }
    } else if (const auto* mask = slot.as<InputSlot::Mask>()) {
        node.linkMask(key, source, mask->signal, mask->channel);
    }
}

void disconnect(GraphKey key, Node& node, const InputSlot& slot)
{
    if (slot.is<InputSlot::Input>())                         one(node).setInput(key, nullptr);
    else if (const auto* layer = slot.as<InputSlot::Layer>()) stack(node).removeLayer(key, layer->index);
    else if (const auto* mask = slot.as<InputSlot::Mask>())   node.linkMask(key, nullptr, mask->signal, mask->channel);
    // a new layer is a position: nothing feeds it
}

void restore(GraphKey key, Node& node, const NodeInputs& want)
{
    node.detachAllInputs(key);
    node.unlinkAllMasks(key);
    if (inputShape(node) == InputShape::One) one(node).setInput(key, want.input);
    if (inputShape(node) == InputShape::Stack)
        for (const NodePtr& l : want.layers) stack(node).addLayer(key, l);
    for (int s = 0; s < kSignalCount; ++s)
        for (int ch = 0; ch < kChannelCount; ++ch)
            if (want.masks[s][ch]) node.linkMask(key, want.masks[s][ch], NodeSignal(s), NodeChannel(ch));
}

void heal(GraphKey key, Node& node, const absl::flat_hash_set<NodeId>& deleted)
{
    if (inputShape(node) == InputShape::One)
        if (NodePtr src = bridge(one(node).input(), deleted)) one(node).setInput(key, src);
    if (inputShape(node) == InputShape::Stack) {
        CompositorNode& c = stack(node);
        for (size_t i = 0; i < c.layers().size(); ++i) {
            NodePtr old = c.layers()[i];   // copy: the replacement edits the stack
            if (NodePtr src = bridge(old, deleted); src && !c.has(src)) c.replaceInputInstance(key, old, src);
        }
    }
    for (int s = 0; s < kSignalCount; ++s)
        for (int ch = 0; ch < kChannelCount; ++ch)
            if (NodePtr src = bridge(node.signal(NodeSignal(s)).mask[ch], deleted)) node.linkMask(key, src, NodeSignal(s), NodeChannel(ch));
}

void emitChanges(NodeId to, const NodeInputs& before, const NodeInputs& now, const EmitEvent& emit)
{
    const auto single = [&](const NodePtr& was, const NodePtr& is, const InputSlot& slot) {
        if (was == is) return;
        if (was) emit({ ProjectEventType::LinkRemoved, to, was->id(), slot });
        if (is)  emit({ ProjectEventType::LinkAdded,   to, is->id(),  slot });
    };
    single(before.input, now.input, InputSlot::input());
    for (int s = 0; s < kSignalCount; ++s)
        for (int ch = 0; ch < kChannelCount; ++ch)
            single(before.masks[s][ch], now.masks[s][ch], InputSlot::mask(NodeSignal(s), NodeChannel(ch)));
    if (before.layers != now.layers) emit({ ProjectEventType::LayersChanged, to, kNoNodeId, {} });
}

}

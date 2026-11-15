#include "node/node-kinds.hpp"

namespace {

constexpr NodeKind kKinds[] = {
    { NodeType::Raster,     "raster",     "Raster",     "pixels you paint",          RasterRootNode::kPorts, true,  WetRule::OwnPixels,
      [](RasterData* pixels) -> NodePtr { return RasterRootNode::create(*pixels); } },
    { NodeType::Reference,  "reference",  "Reference",  "another view of its input", ReferenceNode::kPorts,  false, WetRule::Input,
      [](RasterData*) -> NodePtr { return ReferenceNode::create(); } },
    { NodeType::Move,       "move",       "Move",       "shifts its input",          MoveNode::kPorts,       false, WetRule::MovedInput,
      [](RasterData*) -> NodePtr { return MoveNode::create(); } },
    { NodeType::Compositor, "compositor", "Compositor", "stacks layers",             CompositorNode::kPorts, false, WetRule::Stack,
      [](RasterData*) -> NodePtr { return CompositorNode::create(); } },
};

constexpr bool inTypeOrder()
{
    for (size_t i = 0; i < std::size(kKinds); ++i)
        if (kKinds[i].type != NodeType(i)) return false;
    return true;
}
static_assert(inTypeOrder(), "kKinds rows follow NodeType");

constexpr bool wetRulesFitPorts()   // Computed fits any
{
    for (const NodeKind& k : kKinds) {
        const InputShape in = k.ports.inputs;
        const bool fits = k.wet == WetRule::Computed ||
            (k.wet == WetRule::OwnPixels && in == InputShape::None) ||
            ((k.wet == WetRule::Input || k.wet == WetRule::MovedInput) && in == InputShape::One) ||
            (k.wet == WetRule::Stack && in == InputShape::Stack && k.type == NodeType::Compositor);   // stackDrawn() knows only it
        if (!fits) return false;
    }
    return true;
}
static_assert(wetRulesFitPorts(), "a kind's WetRule matches its ports");

}

std::span<const NodeKind> nodeKinds() { return kKinds; }

const NodeKind& nodeKind(NodeType type) { return kKinds[size_t(type)]; }

const NodeKind* findNodeKind(std::string_view id)
{
    for (const NodeKind& k : kKinds)
        if (id == k.id) return &k;
    return nullptr;
}

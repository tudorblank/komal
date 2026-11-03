#include "node/node-kinds.hpp"

namespace {

constexpr NodeKind kKinds[] = {
    { NodeType::Raster,     "raster",     "Raster",     "pixels you paint",          RasterRootNode::kPorts, true,
      [](RasterData* pixels) -> NodePtr { return RasterRootNode::create(*pixels); } },
    { NodeType::Reference,  "reference",  "Reference",  "another view of its input", ReferenceNode::kPorts,  false,
      [](RasterData*) -> NodePtr { return ReferenceNode::create(); } },
    { NodeType::Move,       "move",       "Move",       "shifts its input",          MoveNode::kPorts,       false,
      [](RasterData*) -> NodePtr { return MoveNode::create(); } },
    { NodeType::Compositor, "compositor", "Compositor", "stacks layers",             CompositorNode::kPorts, false,
      [](RasterData*) -> NodePtr { return CompositorNode::create(); } },
};

constexpr bool inTypeOrder()
{
    for (size_t i = 0; i < std::size(kKinds); ++i)
        if (kKinds[i].type != NodeType(i)) return false;
    return true;
}
static_assert(inTypeOrder(), "kKinds rows follow NodeType");

}

std::span<const NodeKind> nodeKinds() { return kKinds; }

const NodeKind& nodeKind(NodeType type) { return kKinds[size_t(type)]; }

const NodeKind* findNodeKind(std::string_view id)
{
    for (const NodeKind& k : kKinds)
        if (id == k.id) return &k;
    return nullptr;
}

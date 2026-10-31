#include "document/preview-model.hpp"
#include "document/project.hpp"
#include "node/node-kinds.hpp"

// ---------------------------------------------------------------- a node under the edit

float PreviewModel::scale(const Node& n, NodeSignal s, NodeChannel c) const
{
    if (const EditPreview* e = m_project->preview())
        if (const std::optional<float> v = e->scaleOf(n.id(), s, c)) return *v;
    return n.signal(s).scale[int(c)];
}

std::optional<Vec2> PreviewModel::moveOffset(const Node& n) const
{
    const NodeSettings settings = n.settings();
    const auto* move = std::get_if<MoveSettings>(&settings);
    if (!move) return std::nullopt;
    const EditPreview* e = m_project->preview();
    if (const OffsetEdit* o = e ? e->offsetOf(n.id()) : nullptr) return Vec2{ float(o->x), float(o->y) };
    return Vec2{ float(move->x), float(move->y) };
}

bool PreviewModel::wetOn(const Node& n) const
{
    return !n.hasDryWet() || Node::gainByte(scale(n, NodeSignal::Wet, NodeChannel::A)) != 0;
}

bool PreviewModel::dryOn(const Node& n) const
{
    return n.hasDryWet() && Node::gainByte(scale(n, NodeSignal::Dry, NodeChannel::A)) != 0;
}

BoundsI PreviewModel::bounds(NodeId id) const
{
    const int i = m_plan.indexOf(id);
    if (i >= 0) return boundsOf(i, {});
    const Node* n = m_project->find(id);
    return n ? n->bounds() : BoundsI{};
}

// As Node::bounds() works it out, with the edit's values: Wet (moved, recomposed, stroked) and Dry.
BoundsI PreviewModel::boundsOf(int node, Vec2 offset) const
{
    const PreviewPlan::Node& s = m_plan.nodes()[size_t(node)];
    const Node& n = *s.node;
    BoundsI b;
    if (wetOn(n)) {
        if (const std::optional<Vec2> by = moveOffset(n)) {
            b = sourceBounds(s.input, { offset.x + by->x, offset.y + by->y });
        } else if (n.ports().inputs == InputShape::One) {
            b = sourceBounds(s.input, offset);
        } else if (!s.layers.empty()) {
            for (PreviewPlan::Source l : s.layers) b.expand(sourceBounds(l, offset));
        } else if (s.wet >= 0) {
            b = leafBounds(s.wet, offset);
            const BrushParams& brush = m_project->stroke().params();
            if (s.stroke >= 0 && !brush.keepAlpha && brush.mode != CompositeMode::Erase) b.expand(leafBounds(s.stroke, offset));
        }
    }
    if (dryOn(n)) b.expand(sourceBounds(s.input, offset));
    return b;
}

BoundsI PreviewModel::sourceBounds(PreviewPlan::Source src, Vec2 offset) const
{
    if (src.node >= 0) return boundsOf(src.node, offset);
    return src.leaf >= 0 ? leafBounds(src.leaf, offset) : BoundsI{};
}

BoundsI PreviewModel::leafBounds(int leaf, Vec2 offset) const
{
    const PreviewLeaf& l = m_plan.leaves()[size_t(leaf)];
    BoundsI b;
    switch (l.kind) {
    case PreviewLeaf::Kind::Output: b = l.node->bounds(); break;
    case PreviewLeaf::Kind::Wet:    b = l.node->wetBounds(); break;
    case PreviewLeaf::Kind::Stroke: b = m_project->stroke().bounds(); break;
    }
    return b.offsetBy(int(offset.x), int(offset.y));
}

#include "canvas/preview/preview.hpp"
#include "canvas/preview/stroke-source.hpp"
#include "document/project.hpp"
#include "node/node-kinds.hpp"

void Preview::init(const Project& project, TileCache& cache)
{
    m_project = &project;
    m_cache   = &cache;
    m_events  = project.changed.connect([this](const ProjectEvent& e) {
        if (changesGraph(e.type)) m_stale = true;
        if (e.type == ProjectEventType::PreviewChanged || m_stale) follow();   // at once: views read it while drawing
    });
    follow();
}

void Preview::shutdown()
{
    m_events.disconnect();
    drop();
    m_project = nullptr;
    m_cache   = nullptr;
}

const EditPreview& Preview::edit() const
{
    return *m_project->preview();
}

void Preview::update()
{
    follow();
    for (size_t i = 0; i < m_leaves.size(); ++i)   // the cache syncs its own; the stroke is ours
        if (m_plan.leaves()[i].kind == PreviewLeaf::Kind::Stroke) m_leaves[i]->sync();
}

void Preview::follow()
{
    const EditPreview* e = m_project ? m_project->preview() : nullptr;
    if (!e || e->pixelsNode() == kNoNodeId) {   // nothing, or the graph alone
        if (m_active) drop();
        return;
    }
    if (!m_active || m_stale || e->pixelsNode() != m_plan.edited() || e->edit.index() != m_kind) replan(*e);
}

void Preview::replan(const EditPreview& edit)
{
    drop();
    m_stale = false;
    if (!m_plan.build(*m_project, edit)) return;
    for (const PreviewLeaf& leaf : m_plan.leaves()) {
        switch (leaf.kind) {
        case PreviewLeaf::Kind::Output: m_leaves.push_back(m_cache->output(leaf.node)); break;
        case PreviewLeaf::Kind::Wet:    m_leaves.push_back(m_cache->wet(leaf.node)); break;
        case PreviewLeaf::Kind::Stroke: m_leaves.push_back(m_cache->make(std::make_unique<StrokeSource>(*m_project))); break;
        }
    }
    m_kind   = edit.edit.index();
    m_active = true;
    ++m_serial;
}

void Preview::drop()
{
    m_leaves.clear();
    m_plan.clear();
    m_active = false;
    ++m_serial;
}

// ---------------------------------------------------------------- a node under the edit

float Preview::scale(const Node& n, NodeSignal s, NodeChannel c) const
{
    if (const EditPreview* e = m_project->preview())
        if (const std::optional<float> v = e->scaleOf(n.id(), s, c)) return *v;
    return n.signal(s).scale[int(c)];
}

std::optional<Vec2> Preview::moveOffset(const Node& n) const
{
    const NodeSettings settings = n.settings();
    const auto* move = std::get_if<MoveSettings>(&settings);
    if (!move) return std::nullopt;
    const EditPreview* e = m_project->preview();
    if (const OffsetEdit* o = e ? e->offsetOf(n.id()) : nullptr) return Vec2{ float(o->x), float(o->y) };
    return Vec2{ float(move->x), float(move->y) };
}

bool Preview::wetOn(const Node& n) const
{
    return !n.hasDryWet() || Node::gainByte(scale(n, NodeSignal::Wet, NodeChannel::A)) != 0;
}

bool Preview::dryOn(const Node& n) const
{
    return n.hasDryWet() && Node::gainByte(scale(n, NodeSignal::Dry, NodeChannel::A)) != 0;
}

BoundsI Preview::bounds(NodeId id) const
{
    const int i = m_active ? m_plan.indexOf(id) : -1;
    if (i >= 0) return boundsOf(i, {});
    const Node* n = m_project->find(id);
    return n ? n->bounds() : BoundsI{};
}

// As Node::bounds() works it out, with the edit's values: Wet (moved, recomposed, stroked) and Dry.
BoundsI Preview::boundsOf(int node, Vec2 offset) const
{
    const PreviewPlan::Node& s = m_plan.nodes()[size_t(node)];
    const Node& n = *s.node;
    BoundsI b;
    if (wetOn(n)) {
        if (const std::optional<Vec2> by = moveOffset(n)) {
            b = sourceBounds(s.input, { offset.x + by->x, offset.y + by->y });
        } else if (inputShape(n) == InputShape::One) {
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

BoundsI Preview::sourceBounds(PreviewPlan::Source src, Vec2 offset) const
{
    if (src.node >= 0) return boundsOf(src.node, offset);
    return src.leaf >= 0 ? leafBounds(src.leaf, offset) : BoundsI{};
}

BoundsI Preview::leafBounds(int leaf, Vec2 offset) const
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

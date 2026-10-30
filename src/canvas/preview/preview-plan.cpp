#include "canvas/preview/preview-plan.hpp"
#include "document/project.hpp"
#include "node/node-kinds.hpp"

#include <algorithm>

void PreviewPlan::clear()
{
    m_edited = kNoNodeId;
    m_stroke = false;
    m_reached.clear();
    m_index.clear();
    m_nodes.clear();
    m_leaves.clear();
}

int PreviewPlan::indexOf(NodeId id) const
{
    const auto it = m_index.find(id);
    return it == m_index.end() ? -1 : it->second;
}

bool PreviewPlan::build(const Project& project, const EditPreview& edit)
{
    clear();
    const NodePtr edited = project.findPtr(edit.pixelsNode());
    if (!edited) return false;
    m_edited = edited->id();
    m_stroke = edit.stroke() != nullptr;

    // Everything downstream of the edit, through inputs and masks.
    m_reached[edited.get()] = -1;
    for (bool grew = true; grew;) {
        grew = false;
        for (const NodePtr& n : project.nodes()) {
            if (m_reached.contains(n.get())) continue;
            for (const auto& [r, plan] : m_reached)
                if (n->usesAsInput(r) || n->usesAsMask(r)) { m_reached[n.get()] = -1; grew = true; break; }
        }
    }
    for (const NodePtr& n : project.nodes())
        if (m_reached.contains(n.get())) planFor(n);
    return true;
}

int PreviewPlan::leafFor(const NodePtr& n, PreviewLeaf::Kind kind)
{
    for (size_t i = 0; i < m_leaves.size(); ++i)
        if (m_leaves[i].node == n && m_leaves[i].kind == kind) return int(i);
    m_leaves.push_back({ n, kind });
    return int(m_leaves.size()) - 1;
}

PreviewPlan::Source PreviewPlan::sourceFor(const NodePtr& n)
{
    if (m_reached.contains(n.get())) return { -1, planFor(n) };
    return { leafFor(n, PreviewLeaf::Kind::Output), -1 };
}

int PreviewPlan::planFor(const NodePtr& n)
{
    if (const int planned = m_reached.at(n.get()); planned >= 0) return planned;
    Node p;
    p.node = n;
    const NodeInputs in = NodeInputs::of(*n);
    const InputShape shape = inputShape(*n);
    const bool recompose = shape == InputShape::Stack && n->id() != m_edited &&
        std::any_of(in.layers.begin(), in.layers.end(), [&](const NodePtr& l) { return m_reached.contains(l.get()); });
    if (shape == InputShape::One) {
        if (in.input) p.input = sourceFor(in.input);
    } else if (recompose) {   // layers the edit reaches are rebuilt; the rest are drawn as they are
        for (const NodePtr& l : in.layers) p.layers.push_back(sourceFor(l));
    } else {
        p.wet = leafFor(n, PreviewLeaf::Kind::Wet);   // its wet can't change: frozen, its signals change on the GPU
        if (m_stroke && n->id() == m_edited) p.stroke = leafFor(n, PreviewLeaf::Kind::Stroke);
    }
    for (int sig = 0; sig < kSignalCount; ++sig)
        for (int ch = 0; ch < kChannelCount; ++ch)
            if (const NodePtr& m = n->signal(NodeSignal(sig)).mask[ch]; m && n->hasSignal(NodeSignal(sig)))
                p.masks[sig][ch] = sourceFor(m);
    m_nodes.push_back(std::move(p));
    const int index = int(m_nodes.size()) - 1;
    m_reached[n.get()] = index;
    m_index[n->id()] = index;
    return index;
}

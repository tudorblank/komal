#include "graph/scene.hpp"
#include "document/project.hpp"

#include <algorithm>

void GraphScene::init(const Project& project)
{
    m_project = &project;
    m_projectEvents = project.changed.connect([this](const ProjectEvent& e) { onProjectEvent(e); });
    // The ones made before it listened: every item first, then the links between them.
    for (const NodePtr& n : project.nodes()) addItem(n->id(), false);
    for (const NodePtr& n : project.nodes()) addLinksInto(n->id());
}

void GraphScene::shutdown()
{
    m_projectEvents.disconnect();
    m_project = nullptr;
}

const NodeItem* GraphScene::item(NodeId id) const
{
    auto it = m_items.find(id);
    return it == m_items.end() ? nullptr : &it->second;
}

bool GraphScene::isMaster(NodeId id) const
{
    return id != kNoNodeId && m_project->isMaster(id);
}

void GraphScene::bringToFront(NodeId id)
{
    std::erase(m_zOrder, id);
    m_zOrder.push_back(id);
}

void GraphScene::toggleSection(NodeId id, NodeSignal signal)
{
    bool& expanded = m_viewStates[id].expanded[int(signal)];
    expanded = !expanded;
    layoutItem(id);
}

void GraphScene::setStackGap(NodeId node, StackLayout layout)
{
    if (node == m_gapNode && layout == m_gap) return;
    const NodeId was = m_gapNode;
    m_gapNode = node;
    m_gap     = layout;
    if (was != kNoNodeId && was != node) layoutItem(was);
    if (node != kNoNodeId) layoutItem(node);
}

StackLayout GraphScene::stackLayout(NodeId id) const
{
    const Node* n = m_project->find(id);
    if (!n || inputShape(*n) != InputShape::Stack) return {};
    StackLayout l = id == m_gapNode ? m_gap : StackLayout{};
    if (m_lifted && m_lifted->from == id)   // a layer lifted off it: its row closes up
        if (const auto* layer = m_lifted->slot.as<InputSlot::Layer>()) l.hidden = int(layer->index);
    return l;
}

ImVec2 GraphScene::position(NodeId id) const
{
    const Node* n = m_project->find(id);
    if (!n) return {};
    const ImVec2 by = m_moving.contains(id) ? m_movedBy : ImVec2();
    return ImVec2(n->x() + by.x, n->y() + by.y);
}

void GraphScene::followPreview()
{
    const EditPreview* pv = m_project->preview();
    absl::flat_hash_set<NodeId> moving;
    ImVec2 by;
    if (const auto* m = pv ? pv->as<NodesMoveEdit>() : nullptr) {
        moving.insert(m->nodes.begin(), m->nodes.end());
        by = ImVec2(m->dx, m->dy);
    }
    if (moving != m_moving || by.x != m_movedBy.x || by.y != m_movedBy.y) {
        absl::flat_hash_set<NodeId> affected = std::exchange(m_moving, std::move(moving));
        affected.insert(m_moving.begin(), m_moving.end());
        m_movedBy = by;
        for (NodeId id : affected) moveItem(id);
    }

    std::optional<LinkMoveEdit> lifted;
    if (const auto* l = pv ? pv->as<LinkMoveEdit>() : nullptr) lifted = *l;
    if (lifted != m_lifted) {
        const std::optional<LinkMoveEdit> was = std::exchange(m_lifted, lifted);
        if (was) layoutItem(was->from);
        if (m_lifted) layoutItem(m_lifted->from);
        for (GraphLink& l : m_links) placeLinkEnds(l);
        ++m_linksRevision;
    }
}

void GraphScene::onProjectEvent(const ProjectEvent& e)
{
    switch (e.type) {
    case ProjectEventType::NodeAdded:     addItem(e.node); break;
    case ProjectEventType::NodeRemoved:   removeItem(e.node); break;
    case ProjectEventType::NodeMoved:     moveItem(e.node); break;
    case ProjectEventType::LinkAdded:     addLink(e.source, e.node, e.slot); break;
    case ProjectEventType::LinkRemoved:   removeLink(e.node, e.slot); break;
    case ProjectEventType::LayersChanged:
        layoutItem(e.node);
        addLinksInto(e.node, true);
        break;
    case ProjectEventType::PreviewChanged:
        followPreview();
        break;
    default:   // values, pixels, history: painted as they are
        break;
    }
}

void GraphScene::addItem(NodeId id, bool withLinks)
{
    const Node* n = m_project->find(id);
    if (!n || m_items.contains(id)) return;
    NodeItem& item = m_items[id];
    buildNodeItem(*m_project, *n, m_viewStates[id], {}, item);
    if (m_moving.contains(id)) item.moveTo(position(id).x, position(id).y);
    m_zOrder.push_back(id);
    ++m_itemsRevision;
    if (withLinks) addLinksInto(id);   // made already wired (a reference)
}

void GraphScene::removeItem(NodeId id)
{
    m_viewStates.erase(id);
    m_items.erase(id);
    std::erase(m_zOrder, id);
    std::erase_if(m_links, [&](const GraphLink& l) { return l.from == id || l.to == id; });
    ++m_linksRevision;
    ++m_itemsRevision;
}

void GraphScene::moveItem(NodeId id)
{
    auto it = m_items.find(id);
    const Node* n = m_project->find(id);
    if (it == m_items.end() || !n) return;
    const ImVec2 at = position(id);
    it->second.moveTo(at.x, at.y);
    placeLinkEndsOf(id);
    ++m_itemsRevision;
}

void GraphScene::layoutItem(NodeId id)
{
    auto it = m_items.find(id);
    const Node* n = m_project->find(id);
    if (it == m_items.end() || !n) return;
    buildNodeItem(*m_project, *n, m_viewStates[id], stackLayout(id), it->second);
    if (m_moving.contains(id)) it->second.moveTo(position(id).x, position(id).y);
    placeLinkEndsOf(id);
    ++m_itemsRevision;
}

void GraphScene::addLink(NodeId from, NodeId to, const InputSlot& slot)
{
    std::erase_if(m_links, [&](const GraphLink& l) { return l.to == to && l.slot == slot; });   // a slot has one source
    const bool mask = slot.is<InputSlot::Mask>();
    if (mask) layoutItem(to);   // a collapsed section shows every masked channel
    GraphLink l{ from, to, slot, mask, isMaster(to), {}, {} };
    placeLinkEnds(l);
    m_links.push_back(l);
    ++m_linksRevision;
}

void GraphScene::removeLink(NodeId to, const InputSlot& slot)
{
    std::erase_if(m_links, [&](const GraphLink& l) { return l.to == to && l.slot == slot; });
    if (slot.is<InputSlot::Mask>()) layoutItem(to);
    ++m_linksRevision;
}

void GraphScene::addLinksInto(NodeId to, bool layersOnly)
{
    const Node* n = m_project->find(to);
    if (!n) return;
    std::erase_if(m_links, [&](const GraphLink& l) { return l.to == to && (!layersOnly || l.slot.is<InputSlot::Layer>()); });
    ++m_linksRevision;
    for (const auto& [source, slot] : Links::into(*n))
        if (!layersOnly || slot.is<InputSlot::Layer>()) addLink(source->id(), to, slot);
}

void GraphScene::placeLinkEnds(GraphLink& l) const
{
    const NodeItem* from = item(l.from);
    const NodeItem* to   = item(l.to);
    const NodeItemPort* out = from ? from->output() : nullptr;
    const NodeItemPort* in  = to ? to->input(l.slot) : nullptr;
    l.shown = out && in && !lifted(l);
    if (out) l.fromGraph = out->pos;
    if (in)  l.toGraph   = in->pos;
}

void GraphScene::placeLinkEndsOf(NodeId id)
{
    for (GraphLink& l : m_links)
        if (l.from == id || l.to == id) placeLinkEnds(l);
    ++m_linksRevision;
}

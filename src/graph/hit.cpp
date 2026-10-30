#include "graph/hit.hpp"
#include "graph/minimap.hpp"
#include "graph/scene.hpp"

#include <algorithm>

namespace {

bool within(ImVec2 a, ImVec2 b, float r)
{
    const float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy <= r * r;
}

float portReach(const GraphCamera& camera, float contentScale)
{
    return std::max(HitReach::kPort, HitReach::kPortPx * contentScale / camera.scale());
}

}

NodeId GraphHit::node() const
{
    if (const auto* p = as<PortHit>())   return p->node;
    if (const auto* w = as<WidgetHit>()) return w->node;
    if (const auto* n = as<NodeHit>())   return n->node;
    return kNoNodeId;
}

int GraphHit::link() const
{
    if (const auto* l = as<LinkHit>())   return l->link;
    if (const auto* i = as<InsertHit>()) return i->link;
    return -1;
}

int GraphHit::port(NodeId id) const
{
    const auto* p = as<PortHit>();
    return p && p->node == id ? p->port : -1;
}

int GraphHit::widget(NodeId id) const
{
    const auto* w = as<WidgetHit>();
    return w && w->node == id ? w->widget : -1;
}

ImVec2 insertPoint(const LinkCurve& c, ImVec2 mouse)
{
    return linkPoint(c, std::clamp(linkNearest(c, mouse), 0.06f, 0.94f));
}

GraphHit hitNodeItem(NodeId id, const NodeItem& item, ImVec2 p, float reach)
{
    ImRect around = item.bounds;
    around.Expand(reach);
    if (!around.Contains(p)) return {};
    // Ports first: they straddle the box edges.
    for (size_t i = 0; i < item.ports.size(); ++i)
        if (within(p, item.ports[i].pos, reach)) return PortHit{ id, int(i) };
    for (size_t i = 0; i < item.widgets.size(); ++i)
        if (item.widgets[i].rect.Contains(p)) return WidgetHit{ id, int(i) };
    for (const NodeItemBox& b : item.boxes)
        if (b.rect.Contains(p)) return NodeHit{ id };
    return {};
}

GraphHit hitTest(const HitView& v, ImVec2 mouse)
{
    if (const NodeItem* s = v.scene.item(v.pinned)) {
        const GraphHit h = hitNodeItem(v.pinned, *s, v.pinnedCamera.toGraph(mouse), portReach(v.pinnedCamera, v.contentScale));
        if (!h.empty()) return h;
    }
    if (v.minimap.contains(mouse)) return MinimapHit{};

    const ImVec2 p = v.camera.toGraph(mouse);
    const float reach = portReach(v.camera, v.contentScale);
    const std::vector<NodeId>& zOrder = v.scene.zOrder();
    for (auto it = zOrder.rbegin(); it != zOrder.rend(); ++it) {   // front to back
        const NodeItem* s = v.scene.item(*it);
        if (!s || *it == v.pinned) continue;
        const GraphHit h = hitNodeItem(*it, *s, p, reach);
        if (!h.empty()) return h;
    }

    // The insert button sits on top of every link.
    if (v.hoveredLink >= 0 && v.hoveredLink < int(v.curves.size()) && within(mouse, v.insertAt, HitReach::kInsertPx * v.contentScale))
        return InsertHit{ v.hoveredLink };
    int best = -1;
    float bestDist = HitReach::kLinkPx * v.contentScale;
    const std::vector<GraphLink>& links = v.scene.links();
    for (size_t i = 0; i < links.size() && i < v.curves.size(); ++i) {
        if (!links[i].shown) continue;
        const float d = linkDistance(v.curves[i], mouse);
        if (d < bestDist) { bestDist = d; best = int(i); }
    }
    if (best >= 0) return LinkHit{ best };
    return {};
}

const NodeItemPort* portOf(const GraphScene& scene, const PortHit& hit)
{
    const NodeItem* s = scene.item(hit.node);
    return s && hit.port >= 0 && size_t(hit.port) < s->ports.size() ? &s->ports[size_t(hit.port)] : nullptr;
}

const NodeItemWidget* widgetOf(const GraphScene& scene, const WidgetHit& hit)
{
    const NodeItem* s = scene.item(hit.node);
    return s && hit.widget >= 0 && size_t(hit.widget) < s->widgets.size() ? &s->widgets[size_t(hit.widget)] : nullptr;
}

#include "graph/minimap.hpp"
#include "graph/style.hpp"
#include "graph/painter.hpp"

#include <algorithm>

using namespace GraphStyle;

namespace {

constexpr float kMapMargin  = 12.0f;   // from the canvas edges, points
constexpr float kMapInset   = 4.0f;    // map content inside the frame
constexpr float kMapPadding = 60.0f;   // graph units around the nodes
constexpr float kMinW = 60.0f, kMaxW = 200.0f, kMinH = 45.0f, kMaxH = 150.0f;   // points

constexpr ImU32 kMapBack     = IM_COL32( 14,  14,  14, 210);
constexpr ImU32 kMapFrame    = IM_COL32( 46,  46,  46, 255);
constexpr ImU32 kMapNode     = IM_COL32( 78,  78,  78, 255);
constexpr ImU32 kMapHover    = IM_COL32(140, 180, 245, 255);
constexpr ImU32 kMapEdge     = IM_COL32( 96,  96,  96, 255);
constexpr ImU32 kMapMaskEdge = IM_COL32( 70,  70,  70, 255);
constexpr ImU32 kMapViewport = IM_COL32(140, 180, 245, 255);

}

void GraphMinimap::layout(const Items& items, NodeId skip, ImVec2 canvasMin, ImVec2 canvasMax, const ImRect& avoid, float uiScale)
{
    m_items  = &items;
    m_skip    = skip;
    m_uiScale = uiScale;

    ImRect bounds;
    bool any = false;
    for (const auto& [id, s] : items) {
        if (id == skip) continue;
        if (any) bounds.Add(s.bounds); else bounds = s.bounds;
        any = true;
    }
    m_visible = any;
    if (!any) return;
    bounds.Expand(kMapPadding);

    // A quarter of the graph's size, within limits; the content keeps its aspect inside.
    const float w = std::clamp(bounds.GetWidth() / 4.0f, kMinW, kMaxW) * uiScale;
    const float h = std::clamp(bounds.GetHeight() / 4.0f, kMinH, kMaxH) * uiScale;
    const float margin = kMapMargin * uiScale;
    ImVec2 min(canvasMax.x - margin - w, canvasMax.y - margin - h);
    // Bottom-right, unless the pinned master reaches down there: then just left of it.
    if (!avoid.IsInverted() && avoid.Max.y > min.y - margin && avoid.Min.x < min.x + w + margin)
        min.x = avoid.Min.x - margin - w;
    m_rect = ImRect(min, ImVec2(min.x + w, min.y + h));
    if (m_rect.Min.x < canvasMin.x + margin || m_rect.Min.y < canvasMin.y + margin) { m_visible = false; return; }   // no room

    const float inset = kMapInset * uiScale;
    const float iw = w - 2.0f * inset, ih = h - 2.0f * inset;
    m_k = std::min(iw / bounds.GetWidth(), ih / bounds.GetHeight());
    m_offset = ImVec2(m_rect.Min.x + inset + (iw - bounds.GetWidth() * m_k) * 0.5f - bounds.Min.x * m_k,
                      m_rect.Min.y + inset + (ih - bounds.GetHeight() * m_k) * 0.5f - bounds.Min.y * m_k);
}

NodeId GraphMinimap::nodeAt(ImVec2 screen) const
{
    if (!m_items) return kNoNodeId;
    const ImVec2 g = toGraph(screen);
    for (const auto& [id, s] : *m_items)
        if (id != m_skip && s.bounds.Contains(g)) return id;
    return kNoNodeId;
}

void GraphMinimap::press(ImVec2 mouse, GraphCamera& camera, ImVec2 canvasSize) const
{
    if (const NodeId id = nodeAt(mouse); id != kNoNodeId) {
        camera.centerOn(m_items->at(id).bounds.GetCenter(), canvasSize);
        return;
    }
    drag(mouse, camera, canvasSize);
}

void GraphMinimap::drag(ImVec2 mouse, GraphCamera& camera, ImVec2 canvasSize) const
{
    camera.centerOn(toGraph(mouse), canvasSize);
}

void GraphMinimap::paint(ImDrawList* dl, const Project& project, const std::vector<Edge>& edges,
                         const absl::flat_hash_set<NodeId>& selected, const GraphCamera& camera, ImVec2 canvasSize,
                         ImVec2 mouse, bool hoverable) const
{
    if (!m_visible || !m_items) return;
    const float rounding = 4.0f * m_uiScale;
    dl->AddRectFilled(m_rect.Min, m_rect.Max, kMapBack, rounding);
    dl->AddRect(m_rect.Min, m_rect.Max, kMapFrame, rounding);
    dl->PushClipRect(m_rect.Min, m_rect.Max, true);

    // Links under the nodes.
    for (const Edge& e : edges)
        dl->AddLine(toMap(e.from), toMap(e.to), e.mask ? kMapMaskEdge : kMapEdge, 1.0f);

    // Each node as a little node: its box, its header band in its colour.
    const NodeId hovered = hoverable && m_rect.Contains(mouse) ? nodeAt(mouse) : kNoNodeId;
    for (const auto& [id, s] : *m_items) {
        if (id == m_skip) continue;
        const Node* n = project.find(id);
        if (!n) continue;
        const ImVec2 a = toMap(s.bounds.Min), b = toMap(s.bounds.Max);
        const float r = std::min(1.5f * m_uiScale, (b.x - a.x) * 0.25f);
        dl->AddRectFilled(a, b, id == hovered ? kMapHover : kMapNode, r);
        const float headerH = std::max(1.0f, (s.header.GetHeight()) * m_k);
        if (b.y - a.y > headerH * 2.0f)
            dl->AddRectFilled(a, ImVec2(b.x, a.y + headerH), headerColor(project, *n), r, ImDrawFlags_RoundCornersTop);
        if (selected.contains(id)) dl->AddRect(a, b, kSelection, r, 0, 1.0f);
    }

    // What the panel shows.
    const ImVec2 v0 = toMap(camera.toGraph(camera.origin()));
    const ImVec2 v1 = toMap(camera.toGraph(ImVec2(camera.origin().x + canvasSize.x, camera.origin().y + canvasSize.y)));
    dl->AddRect(v0, v1, kMapViewport, 0.0f, 0, 1.5f);

    dl->PopClipRect();
}

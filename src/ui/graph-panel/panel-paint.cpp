#include "ui/graph-panel/panel.hpp"
#include "document/project.hpp"
#include "graph/style.hpp"
#include "ui/theme.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace GraphStyle;


GraphPaint GraphPanel::paintContext(ImDrawList* dl, const GraphCamera& camera) const
{
    return { dl, camera, uiScale(), graphFont(), graphNumberFont() };
}

void GraphPanel::paintGrid(const GraphPaint& g, ImVec2 min, ImVec2 max) const
{
    float step = kGrid * g.scale();
    int major = 4;
    while (step < 12.0f) { step *= 2.0f; major = std::max(1, major / 2); }
    const ImVec2 o = g.at(ImVec2(0.0f, 0.0f));   // a grid line runs through the graph origin
    const int ix0 = int(std::floor((min.x - o.x) / step)), ix1 = int(std::ceil((max.x - o.x) / step));
    const int iy0 = int(std::floor((min.y - o.y) / step)), iy1 = int(std::ceil((max.y - o.y) / step));
    for (int i = ix0; i <= ix1; ++i) {
        const float x = o.x + i * step;
        g.dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), i % major == 0 ? kGridMajor : kGridLine);
    }
    for (int i = iy0; i <= iy1; ++i) {
        const float y = o.y + i * step;
        g.dl->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), i % major == 0 ? kGridMajor : kGridLine);
    }

    // A thin "+" on the origin, a fixed size on screen; on whole pixels, so it stays 1px crisp.
    const ImVec2 c(std::floor(o.x), std::floor(o.y));
    const float arm = std::round(g.px(8.0f));
    g.dl->AddLine(ImVec2(c.x - arm, c.y), ImVec2(c.x + arm + 1.0f, c.y), kOrigin);
    g.dl->AddLine(ImVec2(c.x, c.y - arm), ImVec2(c.x, c.y + arm + 1.0f), kOrigin);
}

void GraphPanel::paint(const GraphPaint& g, const GraphHit& hit, ImVec2 canvasMin, ImVec2 canvasMax)
{
    ImDrawList* dl = g.dl;
    dl->AddRectFilled(canvasMin, canvasMax, kCanvas);
    paintGrid(g, canvasMin, canvasMax);
    const std::string hint = ui().actions.describe({ "RMB / ", Actions::GraphAddNode, ": add   drag ports: link   ", Actions::GraphDelete,
                                                    ": delete   ", Actions::GraphFrameSelected, ": frame" });
    const float fs = ImGui::GetFontSize() * 0.85f;
    dl->AddText(ImGui::GetFont(), fs, ImVec2(canvasMin.x + g.px(8.0f), canvasMax.y - fs - g.px(6.0f)), kDimText, hint.c_str());

    // ---- links, under the nodes
    const int linkHovered = dragging() ? -1 : hit.link();
    const std::vector<GraphLink>& links = m_scene.links();
    for (size_t i = 0; i < links.size(); ++i) {
        const GraphLink& l = links[i];
        if (!l.shown) continue;
        const ImU32 color = l.mask ? kMaskLink : kImageLink;
        if (l.toMaster) paintFadingLink(g, m_curves[i], color, linkHovered == int(i));
        else            paintLink(g, m_curves[i], color, linkHovered == int(i));
    }

    // ---- nodes, back to front; a selection outline belongs to its node's layer
    const auto paintOne = [&](const GraphPaint& pg, NodeId id) {
        const Node* n = ui().project.find(id);
        const NodeItem* s = m_scene.item(id);
        if (!n || !s) return;
        NodeItemVisual v;
        v.selected      = selection().contains(id);
        v.hovered       = hit.node() == id;
        v.hoveredPort   = hit.port(id);
        v.hoveredWidget = dragging() ? -1 : hit.widget(id);
        v.activeWidget  = activeWidget(id);
        v.active        = id == ui().project.activeRasterId();
        paintNode(pg, ui().project, *n, *s, v, ui().thumbnails);
        paintNodeOutline(pg, *s, v);
        paintNodePorts(pg, ui().project, *s, v);

        // The layer row being dragged floats over its stack, still linked to its node.
        const auto* lifted = std::get_if<LayerDrag>(&m_drag);
        if (!lifted || id != lifted->node || !lifted->lifted) return;
        const NodePtr layer = ui().project.inputAt(id, InputSlot::stackLayer(lifted->layer));
        if (!layer) return;
        const float y = pg.camera.toGraph(ImGui::GetIO().MousePos).y - lifted->grab;
        const ImRect row(s->header.Min.x + 6.0f, y, s->header.Max.x - 6.0f, y + kLayerRowH);
        if (const NodeItem* from = m_scene.item(layer->id()); from && from->output()) {
            const ImVec2 a = cameraFor(layer->id()).toScreen(from->output()->pos);
            const ImVec2 b = pg.at(ImVec2(row.Min.x - 6.0f, row.GetCenter().y));
            if (m_scene.isMaster(id)) paintFadingLink(g, linkCurve(g, a, b), kImageLink, false);
            else              paintLink(g, linkCurve(g, a, b), kImageLink, false);
        }
        paintLayerRow(pg, *layer, row, ui().thumbnails, true, true);
    };
    const NodeId master = ui().project.master().id();
    for (NodeId id : m_scene.zOrder())
        if (id != master) paintOne(g, id);

    // ---- the pinned master and the navigator, over the graph
    paintOne(paintContext(dl, m_pinned), master);
    m_minimap.paint(dl, ui().project, m_minimapEdges, selection().ids(), m_camera, m_surface.size(), ImGui::GetIO().MousePos,
                    !dragging() || std::holds_alternative<MinimapDrag>(m_drag));

    // ---- the link being dragged, on top
    if (const auto* link = std::get_if<LinkDrag>(&m_drag)) {
        const NodeItem* s = m_scene.item(link->node);
        const NodeItemPort* fixed = s ? (link->fromOutput ? s->output() : s->input(link->slot)) : nullptr;
        if (fixed) {
            const LinkTarget t = linkTarget(*link, hit);
            const ImVec2 loose = t.valid ? t.pos : ImGui::GetIO().MousePos;
            const ImVec2 anchor = cameraFor(link->node).toScreen(fixed->pos);
            const LinkCurve c = link->fromOutput ? linkCurve(g, anchor, loose) : linkCurve(g, loose, anchor);
            const bool mask = t.valid && t.slot.is<InputSlot::Mask>();
            paintLink(g, c, t.valid ? (mask ? kMaskPort : kImagePort) : kLinkLoose, t.valid);
        }
    }

    if (linkHovered >= 0) paintInsertButton(g, insertPoint(m_curves[size_t(linkHovered)], ImGui::GetIO().MousePos), hit.is<InsertHit>());

    if (std::holds_alternative<RubberBand>(m_drag)) {
        const ImVec2 a = m_press.mouse, b = ImGui::GetIO().MousePos;
        dl->AddRectFilled(ImMin(a, b), ImMax(a, b), kRubberFill);
        dl->AddRect(ImMin(a, b), ImMax(a, b), kRubberEdge, 0.0f, 0, g.px(1.0f));
    }

}

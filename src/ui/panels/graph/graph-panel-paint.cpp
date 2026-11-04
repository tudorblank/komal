#include "ui/panels/graph/graph-panel.hpp"
#include "document/project.hpp"
#include "ui/panels/graph/internal/layout.hpp"
#include "ui/style/graph-style.hpp"
#include "ui/style/fonts.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace GraphLayout;
using namespace GraphStyle;


GraphPaint GraphPanel::paintContext(const GraphCamera& camera) const
{
    return { Painter::window(), camera, uiScale(), Fonts::graph(), Fonts::graphNumbers() };
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
        const float x = o.x + float(i) * step;
        g.draw.line(ImVec2(x, min.y), ImVec2(x, max.y), i % major == 0 ? colors().gridMajor : colors().gridLine);
    }
    for (int i = iy0; i <= iy1; ++i) {
        const float y = o.y + float(i) * step;
        g.draw.line(ImVec2(min.x, y), ImVec2(max.x, y), i % major == 0 ? colors().gridMajor : colors().gridLine);
    }

    // A thin "+" on the origin, a fixed size on screen; on whole pixels, so it stays 1px crisp.
    const ImVec2 c(std::floor(o.x), std::floor(o.y));
    const float arm = std::round(g.px(8.0f));
    g.draw.line(ImVec2(c.x - arm, c.y), ImVec2(c.x + arm + 1.0f, c.y), colors().origin);
    g.draw.line(ImVec2(c.x, c.y - arm), ImVec2(c.x, c.y + arm + 1.0f), colors().origin);
}

void GraphPanel::paint(const GraphPaint& g, const GraphHit& hit, ImVec2 canvasMin, ImVec2 canvasMax)
{
    const Painter& draw = g.draw;
    draw.fillRect(canvasMin, canvasMax, colors().canvas);
    paintGrid(g, canvasMin, canvasMax);
    const std::string hint = ui().actions.describe({ "RMB / ", Actions::GraphAddNode, ": add   drag ports: link   ", Actions::GraphDelete,
                                                    ": delete   ", Actions::GraphFrameSelected, ": frame" });
    const float fs = ImGui::GetFontSize() * kHintText;
    draw.text(ImGui::GetFont(), fs, ImVec2(canvasMin.x + g.px(8.0f), canvasMax.y - fs - g.px(6.0f)), colors().dimText, hint.c_str());

    // ---- links, under the nodes
    const int linkHovered = dragging() ? -1 : hit.link();
    const std::vector<GraphLink>& links = m_scene.links();
    for (size_t i = 0; i < links.size(); ++i) {
        const GraphLink& l = links[i];
        if (!l.shown) continue;
        const ImU32 color = l.mask ? colors().maskLink : colors().imageLink;
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

        // The rows being dragged float over their stack, top first from the pressed one's place,
        // each layer still linked to its node.
        const auto* lifted = std::get_if<LayerDrag>(&m_drag);
        if (!lifted || id != lifted->node || !lifted->lifted) return;
        const auto& rows = lifted->rows;
        // Going into an artboard: see-through, the artboard outlined where it lands.
        const bool intoArtboard = landing(*s, lifted->at).into != kNoArtboard;
        const GraphPaint ghost{ intoArtboard ? pg.draw.faded(0.45f) : pg.draw, pg.camera, pg.contentScale, pg.font, pg.numberFont };
        const size_t pressed = size_t(std::find(rows.begin(), rows.end(), lifted->pressed) - rows.begin());
        float y = pg.camera.toGraph(ImGui::GetIO().MousePos).y - lifted->grab;
        for (size_t i = 0; i < pressed && i < rows.size(); ++i)
            y -= (rows[i].artboard != kNoArtboard ? kArtboardRowH : kLayerRowH) + kLayerGap;
        for (const StackRowRef& r : rows) {
            const float h = r.artboard != kNoArtboard ? kArtboardRowH : kLayerRowH;
            const ImRect row(s->header.Min.x + 6.0f, y, s->header.Max.x - 6.0f, y + h);
            y += h + kLayerGap;
            if (r.artboard != kNoArtboard) {
                paintArtboardRow(ghost, ui().project.artboard(r.artboard), row, { .lifted = true, .selected = true });
                continue;
            }
            const Node* layer = ui().project.find(r.layer);
            if (!layer) continue;
            if (const NodeItem* from = m_scene.item(layer->id()); from && from->output()) {
                const ImVec2 a = cameraFor(layer->id()).toScreen(from->output()->pos);
                const ImVec2 b = pg.at(ImVec2(row.Min.x - 6.0f, row.GetCenter().y));
                if (m_scene.isMaster(id)) paintFadingLink(g, linkCurve(g, a, b), colors().imageLink, false);
                else              paintLink(g, linkCurve(g, a, b), colors().imageLink, false);
            }
            const auto* stack = dynamic_cast<const CompositorNode*>(n);
            paintLayerRow(ghost, *layer, row, ui().thumbnails, { .lifted = true, .selected = true, .muted = stack && stack->muted(*layer) });
        }
    };
    const NodeId master = ui().project.master().id();
    for (NodeId id : m_scene.zOrder())
        if (id != master) paintOne(g, id);

    // ---- the pinned master and the navigator, over the graph
    const GraphPaint pinned = paintContext(m_pinned);
    paintOne(pinned, master);
    {   // its links again, over it: they reach its layers' ports inside it (an artboard's are indented)
        const Painter::Clip clip = draw.clip(m_pinnedRect.Min, m_pinnedRect.Max);
        for (size_t i = 0; i < links.size(); ++i)
            if (links[i].toMaster && links[i].shown)
                paintFadingLink(g, m_curves[i], links[i].mask ? colors().maskLink : colors().imageLink, linkHovered == int(i));
        if (const NodeItem* s = m_scene.item(master)) {   // its ports stay on top
            NodeItemVisual v;
            v.hoveredPort = hit.port(master);
            paintNodePorts(pinned, ui().project, *s, v);
        }
    }
    m_minimap.paint(draw, ui().project, m_minimapEdges, selection().ids(), m_camera, m_surface.size(), ImGui::GetIO().MousePos,
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
            paintLink(g, c, t.valid ? (mask ? colors().maskPort : colors().imagePort) : colors().linkLoose, t.valid);
        }
    }

    if (linkHovered >= 0) paintInsertButton(g, insertPoint(m_curves[size_t(linkHovered)], ImGui::GetIO().MousePos), hit.is<InsertHit>());

    if (std::holds_alternative<RubberBand>(m_drag)) {
        const ImVec2 a = m_press.mouse, b = ImGui::GetIO().MousePos;
        draw.fillRect(ImMin(a, b), ImMax(a, b), colors().rubberFill);
        draw.strokeRect(ImMin(a, b), ImMax(a, b), colors().rubberEdge, 0.0f, g.px(1.0f));
    }

}

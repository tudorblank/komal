#include "graph/panel.hpp"
#include "core/project.hpp"
#include "graph/style.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace GraphStyle;

namespace {

ImU32 withAlpha(ImU32 c, int a) { return (c & ~IM_COL32_A_MASK) | (ImU32(a) << IM_COL32_A_SHIFT); }

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

void GraphPanel::paint(const GraphPaint& g, const Hit& hit, ImVec2 canvasMin, ImVec2 canvasMax)
{
    ImDrawList* dl = g.dl;
    dl->AddRectFilled(canvasMin, canvasMax, kCanvas);
    paintGrid(g, canvasMin, canvasMax);
    const char* hint = "RMB / Shift+A: add   drag ports: link   Del: delete   F: frame";
    const float fs = ImGui::GetFontSize() * 0.85f;
    dl->AddText(ImGui::GetFont(), fs, ImVec2(canvasMin.x + g.px(8.0f), canvasMax.y - fs - g.px(6.0f)), kDimText, hint);

    // ---- links, under the nodes
    const bool linkHovered = m_drag == Drag::None && (hit.kind == Hit::Kind::Link || hit.kind == Hit::Kind::Insert);
    for (size_t i = 0; i < m_links.size(); ++i) {
        const GraphLink& l = m_links[i];
        if (!l.shown) continue;
        const ImU32 color = withAlpha(l.mask ? kMaskPort : kImagePort, 220);
        if (l.toMaster) paintFadingLink(g, l.curve, color, linkHovered && hit.index == int(i));
        else            paintLink(g, l.curve, color, linkHovered && hit.index == int(i));
    }

    // ---- nodes, back to front; a selection outline belongs to its node's layer
    const auto paintOne = [&](const GraphPaint& pg, NodeId id) {
        const Node* n = m_project->find(id);
        const NodeItem* s = item(id);
        if (!n || !s) return;
        NodeItemVisual v;
        v.selected      = m_selection.contains(id);
        v.hovered       = hit.node == id;
        v.hoveredPort   = hit.node == id && hit.kind == Hit::Kind::Port ? hit.index : -1;
        v.hoveredWidget = hit.node == id && hit.kind == Hit::Kind::Widget && m_drag == Drag::None ? hit.index : -1;
        v.activeWidget  = activeWidget(id);
        v.active        = id == m_project->activeRasterId();
        paintNode(pg, *m_project, *n, *s, v, m_thumbs);
        paintNodeOutline(pg, *s, v);
        paintNodePorts(pg, *m_project, *s, v);

        // The layer row being dragged floats over its stack, still linked to its node.
        if (id != m_layerDrag.node || !m_layerDrag.lifted) return;
        const NodePtr layer = m_project->inputAt(id, InputSlot::stackLayer(m_layerDrag.layer));
        if (!layer) return;
        const float y = pg.camera.toGraph(ImGui::GetIO().MousePos).y - m_layerDrag.grab;
        const ImRect row(s->header.Min.x + 6.0f, y, s->header.Max.x - 6.0f, y + kLayerRowH);
        if (const NodeItem* from = item(layer->m_meta.id); from && from->output()) {
            const ImVec2 a = cameraFor(layer->m_meta.id).toScreen(from->output()->pos);
            const ImVec2 b = pg.at(ImVec2(row.Min.x - 6.0f, row.GetCenter().y));
            if (isMaster(id)) paintFadingLink(g, linkCurve(g, a, b), withAlpha(kImagePort, 220), false);
            else              paintLink(g, linkCurve(g, a, b), withAlpha(kImagePort, 220), false);
        }
        paintLayerRow(pg, *layer, row, m_thumbs, true, true);
    };
    const NodeId master = m_project->master().m_meta.id;
    for (NodeId id : m_zOrder)
        if (id != master) paintOne(g, id);

    // ---- the pinned master and the navigator, over the graph
    paintOne(GraphPaint{ dl, m_pinned, m_uiScale }, master);
    m_minimap.paint(dl, *m_project, m_minimapEdges, m_selection.ids(), m_camera, m_canvasSize, ImGui::GetIO().MousePos,
                    m_drag == Drag::None || m_drag == Drag::Minimap);

    // ---- the link being dragged, on top
    if (m_drag == Drag::Link) {
        const NodeItem* s = item(m_link.node);
        const NodeItemPort* fixed = s ? (m_link.fromOutput ? s->output() : s->input(m_link.slot)) : nullptr;
        if (fixed) {
            const LinkTarget t = linkTarget(hit);
            const ImVec2 loose = t.valid ? t.pos : ImGui::GetIO().MousePos;
            const ImVec2 anchor = cameraFor(m_link.node).toScreen(fixed->pos);
            const LinkCurve c = m_link.fromOutput ? linkCurve(g, anchor, loose) : linkCurve(g, loose, anchor);
            const bool mask = t.valid && t.slot.kind == InputSlot::Kind::Mask;
            paintLink(g, c, t.valid ? (mask ? kMaskPort : kImagePort) : IM_COL32(230, 230, 230, 170), t.valid);
        }
    }

    if (linkHovered) paintInsertButton(g, insertPoint(m_links[size_t(hit.index)], ImGui::GetIO().MousePos), hit.kind == Hit::Kind::Insert);

    if (m_drag == Drag::RubberBand) {
        const ImVec2 a = m_pressMouse, b = ImGui::GetIO().MousePos;
        dl->AddRectFilled(ImMin(a, b), ImMax(a, b), kRubberFill);
        dl->AddRect(ImMin(a, b), ImMax(a, b), kRubberEdge, 0.0f, 0, g.px(1.0f));
    }

}

void GraphPanel::drawPopups()
{
    NodeType picked;
    if (m_menu.draw(picked)) applyAdd(picked);

    if (ImGui::BeginPopup(kNodeContext)) {
        const Node* n = m_project->find(m_contextNode);
        if (!n) {
            ImGui::CloseCurrentPopup();
        } else {
            const NodeId id = n->m_meta.id;
            const bool master = m_project->isMaster(id);
            ImGui::TextDisabled("%s", n->m_meta.label.c_str());
            ImGui::Separator();
            if (n->m_meta.type == NodeType::Raster) {
                if (ImGui::MenuItem("Paint into this raster", nullptr, m_project->activeRasterId() == id))
                    m_project->setActiveRaster(id);
                const auto* r = static_cast<const RasterRootNode*>(n);
                if (ImGui::MenuItem("Lock pixels", nullptr, r->locked())) m_project->setRasterLocked(id, !r->locked());
                if (ImGui::MenuItem("Lock transparency", nullptr, r->alphaLocked())) m_project->setRasterAlphaLocked(id, !r->alphaLocked());
            }
            if (!master) {
                const NodeId out = m_project->master().m_meta.id;
                const InputSlot top = InputSlot::newLayer(uint32_t(m_project->master().layers().size()));
                if (ImGui::MenuItem("Add to Master", nullptr, false, m_project->canConnect(id, out, top)))
                    m_project->connect(id, out, top);
                if (ImGui::MenuItem("Reference it")) {
                    const NodeId ref = m_project->createReference(n->m_meta.x + 30.0f, n->m_meta.y + 30.0f, id);
                    m_selection.select(ref);
                }
            }
            const bool many = m_selection.contains(id) && m_selection.size() > 1;
            if (ImGui::MenuItem(many ? "Show selected in Canvas" : "Show in Canvas", "Double-click")) {
                if (many) showInCanvas({ m_selection.ids().begin(), m_selection.ids().end() });
                else      showInCanvas({ id });
            }
            if (auto* move = dynamic_cast<MoveNode*>(m_project->find(id))) {
                // Lost far away: shift it so what it outputs starts at the canvas origin.
                const BoundsI b = move->bounds();
                if (ImGui::MenuItem("Move to Origin", nullptr, false, !b.empty() && (b.minX != 0 || b.minY != 0))) {
                    m_project->setMoveOffset(id, move->offsetX() - b.minX, move->offsetY() - b.minY);
                    showInCanvas({ id });
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Rename...")) {
                ValueEdit e;
                e.kind = ValueEdit::Kind::Rename;
                e.node = id;
                std::snprintf(e.text, sizeof e.text, "%s", n->m_meta.label.c_str());
                openValueEdit(e);
            }
            if (!master) {
                ImGui::Separator();
                if (ImGui::MenuItem(many ? "Delete selected" : "Delete", "Del")) {
                    if (many) deleteSelection();
                    else      m_project->deleteNode(id);
                }
            }
        }
        ImGui::EndPopup();
    }

    // Typed values and names: opened here, at window level, so menus can ask for it too.
    if (m_editPending) {
        ImGui::OpenPopup(kValueEdit);
        m_editPending = false;
    }
    if (ImGui::BeginPopup(kValueEdit)) {
        // One field, typing from the start; the popup closes once it's left (applied or not).
        static constexpr const char* kField = "##value";
        if (m_edit.justOpened) Widgets::focusField(kField);
        const float width = ImGui::GetFontSize() * 10.0f;
        switch (m_edit.kind) {
        case ValueEdit::Kind::Rename:
            if (Widgets::textField(kField, m_edit.text, sizeof m_edit.text, width)) m_project->setNodeLabel(m_edit.node, m_edit.text);
            break;
        case ValueEdit::Kind::Scale:
            if (Widgets::numberField(kField, m_edit.value, 0.0f, 100.0f, "%.1f%%", width))
                m_project->setSignalScale(m_edit.node, m_edit.signal, m_edit.channel, m_edit.value / 100.0f);
            break;
        case ValueEdit::Kind::Offset:
            if (Widgets::numberField(kField, m_edit.value, -1e9f, 1e9f, "%.0f px", width))
                if (const auto* m = dynamic_cast<MoveNode*>(m_project->find(m_edit.node))) {
                    const int v = int(std::lround(m_edit.value));
                    if (m_edit.axis == 0) m_project->setMoveOffset(m_edit.node, v, m->offsetY());
                    else                  m_project->setMoveOffset(m_edit.node, m->offsetX(), v);
                }
            break;
        }
        if (!m_edit.justOpened && !Widgets::isEditing(kField)) ImGui::CloseCurrentPopup();
        m_edit.justOpened = false;
        ImGui::EndPopup();
    }
}

#include "graph/panel.hpp"
#include "core/input.hpp"
#include "core/project.hpp"
#include "graph/style.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace GraphStyle;

namespace {

constexpr float  kZoomStep          = 1.2f;    // per Ctrl+wheel notch
constexpr float  kWheelPanStep      = 24.0f;   // points per wheel notch (as on the canvas)
constexpr double kRightHoldSeconds  = 0.25;    // a right press held this long zooms instead of opening a menu
constexpr float  kRightZoomPerPixel = 1.008f;
constexpr float  kPinScrollStep     = 40.0f;   // points per wheel notch over a tall pinned master

}

void GraphPanel::handleMouse(const Hit& hit, bool hovered)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;

    // ---- navigation
    // Like the canvas: Ctrl+wheel zooms, the wheel scrolls up / down, a sideways wheel left / right.
    if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) && hit.kind != Hit::Kind::Minimap) {
        if (m_input->mods & ModPrimary) m_camera.zoomAt(mouse, std::pow(kZoomStep, io.MouseWheel));
        else if (isMaster(hit.node))    m_pinnedScroll -= io.MouseWheel * kPinScrollStep * m_uiScale;   // clamped by the layout
        else                            m_camera.panBy(ImVec2(io.MouseWheelH * kWheelPanStep, io.MouseWheel * kWheelPanStep));
    }
    if (m_canvasActive && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_camera.panBy(io.MouseDelta);

    // ---- right button: click opens a menu; hold or drag zooms around the press (right / up: in).
    // Ends whenever the button isn't down, so a release we never saw can't leave it on.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hit.kind != Hit::Kind::Minimap) {
        m_rightDown    = true;
        m_rightZooming = false;
        m_rightPress   = mouse;
        m_rightPressAt = ImGui::GetTime();
    }
    if (m_rightDown && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (!m_rightZooming && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::GetTime() - m_rightPressAt >= kRightHoldSeconds))
            m_rightZooming = true;
        const float d = io.MouseDelta.x - io.MouseDelta.y;
        if (m_rightZooming && d != 0.0f) m_camera.zoomAt(m_rightPress, std::pow(kRightZoomPerPixel, d));
    } else if (m_rightDown) {
        m_rightDown = false;
        const bool click = !m_rightZooming;
        m_rightZooming = false;
        if (click && m_drag == Drag::None) {
            if (hit.node != kNoNodeId) {
                m_contextNode = hit.node;
                if (!m_selection.contains(hit.node)) m_selection.select(hit.node);
                ImGui::OpenPopup(kNodeContext);
            } else {
                PendingAdd add;
                add.at = m_camera.toGraph(mouse);
                openAdd(add, NodeMenu::Fit::Any);
            }
        }
    }

    // ---- left button
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const bool dbl = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        m_press      = hit;
        m_pressMouse = mouse;
        m_pressGraph = cameraFor(hit.node).toGraph(mouse);
        m_pressCtrl  = (m_input->mods & ModPrimary) != 0;   // Cmd on macOS
        m_pressShift = (m_input->mods & ModShift) != 0;
        switch (hit.kind) {
        case Hit::Kind::Port:   startLink(hit); break;
        case Hit::Kind::Widget: pressWidget(hit, dbl); break;
        case Hit::Kind::Node:
            pressNode(hit.node);
            if (dbl) showInCanvas({ hit.node });   // the canvas goes to where its pixels are
            break;
        case Hit::Kind::Minimap:
            m_drag = Drag::Minimap;
            m_minimap.press(mouse, m_camera, m_canvasSize);
            break;
        case Hit::Kind::Insert: {
            const GraphLink& l = m_links[size_t(hit.index)];
            PendingAdd add;
            add.mode = PendingAdd::Mode::Insert;
            add.at   = m_camera.toGraph(insertPoint(l, mouse));   // the node lands right there
            add.from = l.from;
            add.to   = l.to;
            add.slot = l.slot;
            openAdd(add, NodeMenu::Fit::NeedsInput);
            break;
        }
        case Hit::Kind::Link:
        case Hit::Kind::None:
            if (dbl) {
                PendingAdd add;
                add.at = m_pressGraph;
                openAdd(add, NodeMenu::Fit::Any);
                break;
            }
            m_drag = Drag::Empty;
            m_bandBase = (m_pressCtrl || m_pressShift) ? m_selection.ids() : GraphSelection::Set{};
            break;
        }
    }
    if (m_drag != Drag::None && ImGui::IsMouseDown(ImGuiMouseButton_Left)) updateDrag();
    if (m_drag != Drag::None && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        switch (m_drag) {
        case Drag::Node:   // a click (no drag) on a node of a bigger selection switches to it
            if (m_pressWasSelected) m_selection.select(m_press.node);
            break;
        case Drag::Empty:  // a click on nothing
            if (!m_pressCtrl && !m_pressShift) m_selection.clear();
            break;
        case Drag::Link:
            finishLink(hit);
            break;
        case Drag::Scale:
        case Drag::Offset:   // the drag is over: now the graph changes, once
            m_project->applyPreview();
            break;
        case Drag::Layer:    // the stack changes once, where the gap is
            if (m_layerDrag.lifted && m_layerDrag.at >= 0 && m_layerDrag.at != int(m_layerDrag.layer))
                m_project->moveLayer(m_layerDrag.node, m_layerDrag.layer, size_t(m_layerDrag.at));
            m_layerDrag = {};
            break;
        default:
            break;
        }
        m_drag = Drag::None;
    }
}

void GraphPanel::pressNode(NodeId id)
{
    if (isMaster(id)) {   // pinned: selected, never moved
        m_selection.select(id);
        return;
    }
    bringToFront(id);
    m_pressWasSelected = false;
    if (m_pressShift) {
        m_selection.chain(*m_project, id, m_pressCtrl);
    } else if (m_pressCtrl) {
        m_selection.toggle(id);
    } else {
        m_pressWasSelected = m_selection.contains(id) && m_selection.size() > 1;
        if (!m_selection.contains(id)) m_selection.select(id);
        m_selection.setAnchor(id);
        if (Node* n = m_project->find(id); n && n->m_meta.type == NodeType::Raster)
            m_project->setActiveRaster(id);   // clicking a raster makes it the one the canvas paints
    }
    if (!m_selection.contains(id)) return;   // Ctrl+click took it out: nothing to drag

    m_drag = Drag::Node;
    m_moveStart.clear();
    for (NodeId sel : m_selection.ids())
        if (Node* n = m_project->find(sel)) m_moveStart.emplace_back(sel, ImVec2(n->m_meta.x, n->m_meta.y));
}

void GraphPanel::pressWidget(const Hit& hit, bool doubleClick)
{
    const NodeItem* s = item(hit.node);
    Node* n = m_project->find(hit.node);
    if (!s || !n) return;
    const NodeItemWidget w = s->widgets[size_t(hit.index)];
    m_pressWidget = w;
    bringToFront(hit.node);

    switch (w.control) {
    case NodeItemControl::Title:
        if (doubleClick) {
            ValueEdit e;
            e.kind = ValueEdit::Kind::Rename;
            e.node = hit.node;
            std::snprintf(e.text, sizeof e.text, "%s", n->m_meta.label.c_str());
            openValueEdit(e);
        } else {
            pressNode(hit.node);
        }
        break;
    case NodeItemControl::Lock:
        m_project->setRasterLocked(hit.node, !static_cast<RasterRootNode*>(n)->locked());
        break;
    case NodeItemControl::LockAlpha:
        m_project->setRasterAlphaLocked(hit.node, !static_cast<RasterRootNode*>(n)->alphaLocked());
        break;
    case NodeItemControl::SectionToggle: {
        bool& e = m_viewStates[hit.node].expanded[int(w.signal)];
        e = !e;
        break;
    }
    case NodeItemControl::Scale:
        if (doubleClick) {
            ValueEdit e;
            e.kind    = ValueEdit::Kind::Scale;
            e.node    = hit.node;
            e.signal  = w.signal;
            e.channel = w.channel;
            e.value   = std::round(n->signal(w.signal).scale[int(w.channel)] * 1000.0f) / 10.0f;
            openValueEdit(e);
            break;
        }
        for (const NodeItemChannelRow& row : s->channels)   // the track drags; the value text doesn't
            if (row.signal == w.signal && row.channel == w.channel && m_pressGraph.x <= row.track.Max.x + 5.0f)
                m_drag = Drag::Scale;
        break;
    case NodeItemControl::OffsetX:
    case NodeItemControl::OffsetY: {
        const auto* m = static_cast<MoveNode*>(n);
        const int axis = w.control == NodeItemControl::OffsetX ? 0 : 1;
        if (doubleClick) {
            ValueEdit e;
            e.kind  = ValueEdit::Kind::Offset;
            e.node  = hit.node;
            e.axis  = axis;
            e.value = float(axis == 0 ? m->offsetX() : m->offsetY());
            openValueEdit(e);
            break;
        }
        m_drag = Drag::Offset;
        m_dragValue = axis == 0 ? m->offsetX() : m->offsetY();
        break;
    }
    case NodeItemControl::Layer:
        m_drag = Drag::Layer;
        m_layerDrag = { hit.node, w.layer, m_pressGraph.y - w.rect.Min.y, false, -1 };
        break;
    }
}

void GraphPanel::updateDrag()
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const ImVec2 gm = cameraFor(m_press.node).toGraph(mouse);
    const bool moved = ImGui::IsMouseDragging(ImGuiMouseButton_Left);

    switch (m_drag) {
    case Drag::Node:
        if (!moved) break;
        m_drag = Drag::MoveNodes;
        [[fallthrough]];
    case Drag::MoveNodes:
        for (const auto& [id, start] : m_moveStart)
            m_project->setNodePosition(id, start.x + gm.x - m_pressGraph.x, start.y + gm.y - m_pressGraph.y);
        break;

    case Drag::Empty:
        if (!moved) break;
        m_drag = Drag::RubberBand;
        [[fallthrough]];
    case Drag::RubberBand: {
        const ImRect band(ImMin(m_pressGraph, gm), ImMax(m_pressGraph, gm));
        GraphSelection::Set next = m_bandBase;
        for (const auto& [id, s] : m_items) {
            if (isMaster(id) || !s.bounds.Overlaps(band)) continue;   // the master isn't in this space
            if (m_pressCtrl && !m_pressShift && m_bandBase.contains(id)) next.erase(id);   // Ctrl: toggle
            else next.insert(id);
        }
        m_selection.assign(std::move(next));
        break;
    }

    case Drag::Scale: {
        const NodeItem* s = item(m_press.node);
        if (!s) break;
        for (const NodeItemChannelRow& row : s->channels)
            if (row.signal == m_pressWidget.signal && row.channel == m_pressWidget.channel) {
                // Previewed only: the graph changes once, on release (see EditPreview).
                EditPreview e;
                e.kind    = EditPreview::Kind::SignalScale;
                e.node    = m_press.node;
                e.signal  = row.signal;
                e.channel = row.channel;
                e.scale   = std::round(std::clamp((gm.x - row.track.Min.x) / row.track.GetWidth(), 0.0f, 1.0f) * 1000.0f) / 1000.0f;
                m_project->previewEdit(e);
            }
        break;
    }

    case Drag::Offset: {
        const auto* m = dynamic_cast<MoveNode*>(m_project->find(m_press.node));
        if (!m) break;
        const int v = m_dragValue + int(std::lround(gm.x - m_pressGraph.x));   // one pixel per graph unit
        EditPreview e;   // previewed only, applied on release
        e.kind = EditPreview::Kind::MoveOffset;
        e.node = m_press.node;
        e.x    = m_pressWidget.control == NodeItemControl::OffsetX ? v : m->offsetX();
        e.y    = m_pressWidget.control == NodeItemControl::OffsetY ? v : m->offsetY();
        m_project->previewEdit(e);
        break;
    }

    case Drag::Layer: {
        const NodeItem* s = item(m_layerDrag.node);
        if (!s) break;
        if (!m_layerDrag.lifted) {
            if (!moved) break;
            m_layerDrag.lifted = true;
        }
        // Pulled out sideways: the layer comes off the stack, carried as a link.
        constexpr float kPullOut = 40.0f;
        if (gm.x < s->bounds.Min.x - kPullOut || gm.x > s->bounds.Max.x + kPullOut) {
            const NodePtr layer = m_project->inputAt(m_layerDrag.node, InputSlot::stackLayer(m_layerDrag.layer));
            const NodeId stack = m_layerDrag.node;
            const uint32_t index = m_layerDrag.layer;
            m_layerDrag = {};
            m_drag = Drag::None;
            if (layer) {
                m_project->disconnect(stack, InputSlot::stackLayer(index));
                m_link = {};
                m_link.node = layer->m_meta.id;
                m_link.pickedUp = true;
                m_drag = Drag::Link;
            }
            break;
        }
        // Where the floating row's middle is: the gap opens there.
        m_layerDrag.at = int(stackPosition(m_layerDrag.node, gm.y - m_layerDrag.grab + kLayerRowH * 0.5f));
        break;
    }

    case Drag::Minimap:   // held on the navigator: the view follows the mouse
        if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f) m_minimap.drag(mouse, m_camera, m_canvasSize);
        break;

    default:
        break;
    }
}

void GraphPanel::startLink(const Hit& hit)
{
    const NodeItemPort& p = item(hit.node)->ports[size_t(hit.index)];
    m_link = {};
    if (p.output) {
        m_link.node = hit.node;
    } else if (const NodePtr src = m_project->inputAt(hit.node, p.slot)) {
        // Lift the link off the input and carry it from its source.
        m_link.node     = src->m_meta.id;
        m_link.pickedUp = true;
        m_project->disconnect(hit.node, p.slot);
    } else {
        m_link.node       = hit.node;
        m_link.fromOutput = false;
        m_link.slot       = p.slot;
    }
    m_drag = Drag::Link;
}

uint32_t GraphPanel::stackPosition(NodeId id, float y) const
{
    // Above every layer row whose middle is below y (so an open gap stays until y passes a middle).
    const NodeItem* s = item(id);
    uint32_t at = 0;
    if (s)
        for (const NodeItemLayerRow& row : s->layers)
            if (row.kind == NodeItemLayerRow::Kind::Layer && row.rect.GetCenter().y > y) ++at;
    return at;
}

GraphPanel::LinkTarget GraphPanel::linkTarget(const Hit& hit) const
{
    LinkTarget t;
    // Anywhere over a compositor (but its mask inputs): inserted into its stack, at the mouse.
    if (m_link.fromOutput && dynamic_cast<const CompositorNode*>(m_project->find(hit.node)) &&
        (hit.kind == Hit::Kind::Node || hit.kind == Hit::Kind::Widget ||
         (hit.kind == Hit::Kind::Port && !item(hit.node)->ports[size_t(hit.index)].mask))) {
        const InputSlot slot = InputSlot::newLayer(stackPosition(hit.node, cameraFor(hit.node).toGraph(ImGui::GetIO().MousePos).y));
        if (!m_project->canConnect(m_link.node, hit.node, slot)) return t;
        const NodeItemPort* p = item(hit.node)->input(slot);   // the gap's port, once it is open
        return { true, hit.node, slot, p ? cameraFor(hit.node).toScreen(p->pos) : ImGui::GetIO().MousePos };
    }
    if (hit.kind == Hit::Kind::Port) {
        const NodeItemPort& p = item(hit.node)->ports[size_t(hit.index)];
        if (m_link.fromOutput && !p.output && m_project->canConnect(m_link.node, hit.node, p.slot))
            t = { true, hit.node, p.slot, cameraFor(hit.node).toScreen(p.pos) };
        else if (!m_link.fromOutput && p.output && m_project->canConnect(hit.node, m_link.node, m_link.slot))
            t = { true, hit.node, {}, cameraFor(hit.node).toScreen(p.pos) };
    } else if ((hit.kind == Hit::Kind::Node || hit.kind == Hit::Kind::Widget) && m_link.fromOutput) {
        // Dropped on a node's body: its natural input (a unary input, a new top layer).
        InputSlot slot;
        const Node* n = m_project->find(hit.node);
        if (n && preferredInput(*n, slot) && m_project->canConnect(m_link.node, hit.node, slot))
            if (const NodeItemPort* p = item(hit.node)->input(slot)) t = { true, hit.node, slot, cameraFor(hit.node).toScreen(p->pos) };
    }
    return t;
}

void GraphPanel::finishLink(const Hit& hit)
{
    const LinkTarget t = linkTarget(hit);
    if (t.valid) {
        if (m_link.fromOutput) m_project->connect(m_link.node, t.node, t.slot);
        else                   m_project->connect(t.node, m_link.node, m_link.slot);
        return;
    }
    if (m_link.pickedUp) return;   // lifted off and dropped on nothing: stays disconnected
    if (hit.kind != Hit::Kind::None && hit.kind != Hit::Kind::Link && hit.kind != Hit::Kind::Insert) return;

    // Dropped on empty space: pick a node to create there, already connected.
    PendingAdd add;
    add.at = m_camera.toGraph(ImGui::GetIO().MousePos);
    if (m_link.fromOutput) {
        add.mode = PendingAdd::Mode::FromOutput;
        add.from = m_link.node;
        openAdd(add, NodeMenu::Fit::NeedsInput);
    } else {
        add.mode = PendingAdd::Mode::ToInput;
        add.to   = m_link.node;
        add.slot = m_link.slot;
        openAdd(add, NodeMenu::Fit::NeedsOutput);
    }
}

void GraphPanel::openAdd(const PendingAdd& add, NodeMenu::Fit fit)
{
    m_pendingAdd = add;
    m_menu.open(fit);
}

void GraphPanel::applyAdd(NodeType type)
{
    const PendingAdd& a = m_pendingAdd;
    const float portY = kHeaderH + kIoRowH * 0.5f;   // where a node's I/O row sits, below its top
    float x = a.at.x, y = a.at.y;
    switch (a.mode) {
    case PendingAdd::Mode::Free:                                              break;
    case PendingAdd::Mode::FromOutput:                        y -= portY;     break;   // input port at the mouse
    case PendingAdd::Mode::ToInput:    x -= kNodeWidth;        y -= portY;     break;   // output port at the mouse
    case PendingAdd::Mode::Insert:     x -= kNodeWidth * 0.5f; y -= portY;     break;   // centred where the link was clicked
    }
    const NodeId id = m_project->createNode(type, x, y);
    const Node* n = m_project->find(id);
    if (!n) return;

    InputSlot in;
    const bool hasInput = preferredInput(*n, in);
    switch (a.mode) {
    case PendingAdd::Mode::Free:
        break;
    case PendingAdd::Mode::FromOutput:
        if (hasInput) m_project->connect(a.from, id, in);
        break;
    case PendingAdd::Mode::ToInput:
        m_project->connect(id, a.to, a.slot);
        break;
    case PendingAdd::Mode::Insert:
        if (hasInput && m_project->connect(a.from, id, in)) m_project->connect(id, a.to, a.slot);
        break;
    }
    m_selection.select(id);
    bringToFront(id);
}

// Where on a link its insert button is: the point nearest to the mouse, kept off the ports at
// its ends.
ImVec2 GraphPanel::insertPoint(const GraphLink& l, ImVec2 mouse) const
{
    return linkPoint(l.curve, std::clamp(linkNearest(l.curve, mouse), 0.06f, 0.94f));
}

void GraphPanel::openValueEdit(const ValueEdit& edit)
{
    m_edit = edit;
    m_edit.justOpened = true;
    m_editPending = true;
}

int GraphPanel::activeWidget(NodeId id) const
{
    if (id != m_press.node || (m_drag != Drag::Scale && m_drag != Drag::Offset)) return -1;
    const NodeItem* s = item(id);
    if (!s) return -1;
    for (size_t i = 0; i < s->widgets.size(); ++i) {
        const NodeItemWidget& w = s->widgets[i];
        if (w.control != m_pressWidget.control) continue;
        if (w.signal == m_pressWidget.signal && w.channel == m_pressWidget.channel) return int(i);
    }
    return -1;
}

void GraphPanel::setCursor(const Hit& hit) const
{
    ImGuiMouseCursor c = ImGuiMouseCursor_Arrow;
    if (m_rightZooming) c = ImGuiMouseCursor_ResizeNS;
    else if (m_canvasActive && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) c = ImGuiMouseCursor_ResizeAll;
    else if (m_drag == Drag::Scale || m_drag == Drag::Offset) c = ImGuiMouseCursor_ResizeEW;
    else if (m_drag == Drag::Layer) c = ImGuiMouseCursor_ResizeNS;
    else if (m_drag == Drag::Link) c = ImGuiMouseCursor_Hand;
    else if (m_drag == Drag::None) {
        if (hit.kind == Hit::Kind::Port || hit.kind == Hit::Kind::Insert) c = ImGuiMouseCursor_Hand;
        if (hit.kind == Hit::Kind::Widget) {
            switch (item(hit.node)->widgets[size_t(hit.index)].control) {
            case NodeItemControl::Lock:
            case NodeItemControl::LockAlpha:
            case NodeItemControl::SectionToggle: c = ImGuiMouseCursor_Hand; break;
            case NodeItemControl::Scale:
            case NodeItemControl::OffsetX:
            case NodeItemControl::OffsetY:       c = ImGuiMouseCursor_ResizeEW; break;
            case NodeItemControl::Layer:         c = ImGuiMouseCursor_ResizeNS; break;
            case NodeItemControl::Title:         break;
            }
        }
    }
    ImGui::SetMouseCursor(c);
}

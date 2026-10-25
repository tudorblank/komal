#include "graph-panel.h"
#include "core/project.h"
#include "gfx/gpu-context.h"
#include "ui/graph-view/graph-style.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace GraphStyle;

namespace {

constexpr double kThumbBudgetMs  = 3.0;     // per frame
constexpr float  kZoomStep       = 1.2f;    // per wheel notch
constexpr float  kLinkHoverPx    = 6.0f;
constexpr float  kInsertRadiusPx = 10.0f;
constexpr float  kFrameMargin    = 40.0f;   // points around framed nodes
constexpr float  kPinMargin      = 12.0f;   // points between the pinned master and the panel's edges
constexpr float  kPinScrollStep  = 40.0f;   // points per wheel notch over a master taller than the panel
constexpr const char* kNodeContext = "##node-context";
constexpr const char* kValueEdit   = "##value-edit";

ImU32 withAlpha(ImU32 c, int a) { return (c & ~IM_COL32_A_MASK) | (ImU32(a) << IM_COL32_A_SHIFT); }

} // namespace

void GraphPanel::init(GpuContext& gpu, Project& project)
{
    m_project = &project;
    m_thumbs.init(gpu.device(), gpu.queue());
    m_subscription = project.subscribe([this](const ProjectEvent& e) {
        if (e.type != ProjectEventType::NodeRemoved) return;
        m_selection.erase(e.node);
        m_viewStates.erase(e.node);
        m_shapes.erase(e.node);
        m_thumbs.forget(e.node);
        std::erase(m_zOrder, e.node);
    });
}

void GraphPanel::shutdown()
{
    if (m_project) m_project->unsubscribe(m_subscription);
    m_project = nullptr;
    m_thumbs.shutdown();
}

// ---------------------------------------------------------------- frame

void GraphPanel::draw(bool* open)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool shown = ImGui::Begin("Graph", open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    m_window = nullptr;
    if (!shown || size.x < 1.0f || size.y < 1.0f) { ImGui::End(); return; }

    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    m_window    = ImGui::GetCurrentWindowRead();
    m_canvasMin = canvasMin;
    const ImVec2 canvasMax(canvasMin.x + size.x, canvasMin.y + size.y);
    // One item covers the canvas: it owns the mouse while a button is held, so drags never
    // move the window or reach widgets behind it.
    ImGui::InvisibleButton("##graph", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                            ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    m_canvasActive = ImGui::IsItemActive();
    m_camera.setFrame(canvasMin, m_uiScale);
    m_canvasSize = size;

    syncNodes();
    layoutOverlays(canvasMin, canvasMax);
    if (m_frameOnStart > 0) {
        frameNodes(false, size);
        --m_frameOnStart;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const GraphPaint g = paintContext(dl);
    rebuildLinks(g);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool tracking = hovered || m_drag != Drag::None;
    Hit hit = tracking ? hitTest(mouse, g) : Hit{};
    handleMouse(hit, hovered);
    handleKeys(size);

    // Input may have changed the project: draw it as it is now.
    syncNodes();
    layoutOverlays(canvasMin, canvasMax);
    rebuildLinks(g);
    hit = tracking ? hitTest(mouse, g) : Hit{};
    m_hoveredLink = (hit.kind == Hit::Kind::Link || hit.kind == Hit::Kind::Insert) ? hit.index : -1;
    // A link dragged over a stack opens a gap where it would land (laid out from next frame on).
    m_pendingLayer = {};
    if (m_drag == Drag::Link)
        if (const LinkTarget t = linkTarget(hit); t.valid && t.slot.kind == InputSlot::Kind::NewLayer)
            m_pendingLayer = { t.node, int(t.slot.layer) };
    if (tracking) setCursor(hit);

    dl->PushClipRect(canvasMin, canvasMax, true);
    paint(g, hit, canvasMin, canvasMax);
    dl->PopClipRect();

    drawPopups();
    ImGui::End();
    m_thumbs.refresh(*m_project, kThumbBudgetMs);
}

bool GraphPanel::dropTarget(const ImGuiWindow* window, ImVec2 screen, ImVec2& graph) const
{
    if (!m_window || !window || window->RootWindow != m_window) return false;
    if (screen.x < m_canvasMin.x || screen.y < m_canvasMin.y ||
        screen.x >= m_canvasMin.x + m_canvasSize.x || screen.y >= m_canvasMin.y + m_canvasSize.y) return false;
    graph = m_camera.toGraph(screen);
    return true;
}

void GraphPanel::selectOnly(NodeId id)
{
    m_selection.select(id);
    bringToFront(id);
}

void GraphPanel::syncNodes()
{
    m_shapes.clear();
    for (const NodePtr& n : m_project->nodes()) {
        const NodeId id = n->m_meta.id;
        StackLayout layout;
        if (id == m_layerDrag.node && m_layerDrag.lifted) layout = { m_layerDrag.at, int(m_layerDrag.layer) };
        else if (id == m_pendingLayer.node)               layout.gap = m_pendingLayer.at;
        m_shapes.emplace(id, buildNodeShape(*m_project, *n, m_viewStates[id], layout));
        if (std::find(m_zOrder.begin(), m_zOrder.end(), id) == m_zOrder.end()) m_zOrder.push_back(id);
    }
    std::erase_if(m_zOrder, [&](NodeId id) { return !m_shapes.contains(id); });
}

bool GraphPanel::isMaster(NodeId id) const
{
    return id != kNoNodeId && m_project->isMaster(id);
}

void GraphPanel::layoutOverlays(ImVec2 canvasMin, ImVec2 canvasMax)
{
    // The master's camera: 100%, its shape's top-right corner at the panel's (less a margin),
    // scrolled when it is taller than the panel.
    const NodeId master = m_project->master().m_meta.id;
    m_pinned.zoom = 1.0f;
    m_pinned.setFrame(canvasMin, m_uiScale);
    m_pinnedRect = ImRect(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
    if (const NodeShape* s = shape(master)) {
        const float k = m_pinned.scale(), margin = kPinMargin * m_uiScale;
        const float maxScroll = std::max(0.0f, s->bounds.GetHeight() * k + 2.0f * margin - (canvasMax.y - canvasMin.y));
        m_pinnedScroll = std::clamp(m_pinnedScroll, 0.0f, maxScroll);
        m_pinned.position = ImVec2(s->bounds.Max.x - (canvasMax.x - margin - canvasMin.x) / k,
                                   s->bounds.Min.y - (margin - m_pinnedScroll) / k);
        m_pinnedRect = ImRect(m_pinned.toScreen(s->bounds.Min), m_pinned.toScreen(s->bounds.Max));
    }
    m_minimap.layout(m_shapes, master, canvasMin, canvasMax, m_pinnedRect, m_uiScale);
}

void GraphPanel::rebuildLinks(const GraphPaint& g)
{
    m_links.clear();
    for (const NodePtr& n : m_project->nodes()) {
        const NodeId to = n->m_meta.id;
        const NodeShape* dst = shape(to);
        for (const NodePort& p : dst->ports) {
            if (p.output) continue;
            const NodePtr src = m_project->inputAt(to, p.slot);
            const NodeShape* from = src ? shape(src->m_meta.id) : nullptr;
            const NodePort* out = from ? from->output() : nullptr;
            if (!out) continue;
            const ImVec2 a = cameraFor(src->m_meta.id).toScreen(out->pos), b = cameraFor(to).toScreen(p.pos);
            m_links.push_back({ src->m_meta.id, to, p.slot, p.mask, isMaster(to), linkCurve(g, a, b), out->pos, p.pos });
        }
    }
}

const NodeShape* GraphPanel::shape(NodeId id) const
{
    auto it = m_shapes.find(id);
    return it == m_shapes.end() ? nullptr : &it->second;
}

void GraphPanel::bringToFront(NodeId id)
{
    std::erase(m_zOrder, id);
    m_zOrder.push_back(id);
}

GraphPanel::Hit GraphPanel::hitNode(NodeId id, const NodeShape& s, ImVec2 p, float portR) const
{
    ImRect reach = s.bounds;
    reach.Expand(portR);
    if (!reach.Contains(p)) return {};
    // Ports first: they straddle the box edges.
    for (size_t i = 0; i < s.ports.size(); ++i) {
        const float dx = p.x - s.ports[i].pos.x, dy = p.y - s.ports[i].pos.y;
        if (dx * dx + dy * dy <= portR * portR) return { Hit::Kind::Port, id, int(i) };
    }
    for (size_t i = 0; i < s.widgets.size(); ++i)
        if (s.widgets[i].rect.Contains(p)) return { Hit::Kind::Widget, id, int(i) };
    for (const NodeBox& b : s.boxes)
        if (b.rect.Contains(p)) return { Hit::Kind::Node, id, -1 };
    return {};
}

GraphPanel::Hit GraphPanel::hitTest(ImVec2 mouse, const GraphPaint& g) const
{
    // The pinned master and the navigator float over everything.
    const NodeId master = m_project->master().m_meta.id;
    if (const NodeShape* s = shape(master)) {
        const Hit h = hitNode(master, *s, m_pinned.toGraph(mouse), std::max(kPortHit, g.px(7.0f) / m_pinned.scale()));
        if (h.kind != Hit::Kind::None) return h;
    }
    if (m_minimap.contains(mouse)) return { Hit::Kind::Minimap, kNoNodeId, -1 };

    const ImVec2 p = m_camera.toGraph(mouse);
    const float portR = std::max(kPortHit, g.px(7.0f) / g.scale());
    for (auto it = m_zOrder.rbegin(); it != m_zOrder.rend(); ++it) {   // front to back
        const NodeShape* s = shape(*it);
        if (!s || *it == master) continue;
        const Hit h = hitNode(*it, *s, p, portR);
        if (h.kind != Hit::Kind::None) return h;
    }

    // The insert button of the link hovered last frame sits on top of every link.
    if (m_hoveredLink >= 0 && m_hoveredLink < int(m_links.size())) {
        const ImVec2 mid = linkPoint(m_links[size_t(m_hoveredLink)].curve, 0.5f);
        const float dx = mouse.x - mid.x, dy = mouse.y - mid.y;
        if (dx * dx + dy * dy <= g.px(kInsertRadiusPx) * g.px(kInsertRadiusPx)) return { Hit::Kind::Insert, kNoNodeId, m_hoveredLink };
    }
    int best = -1;
    float bestDist = g.px(kLinkHoverPx);
    for (size_t i = 0; i < m_links.size(); ++i) {
        const float d = linkDistance(m_links[i].curve, mouse);
        if (d < bestDist) { bestDist = d; best = int(i); }
    }
    if (best >= 0) return { Hit::Kind::Link, kNoNodeId, best };
    return {};
}

// ---------------------------------------------------------------- input

void GraphPanel::handleMouse(const Hit& hit, bool hovered)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;

    // ---- navigation
    if (hovered && io.MouseWheel != 0.0f) {
        if (isMaster(hit.node)) m_pinnedScroll -= io.MouseWheel * kPinScrollStep * m_uiScale;   // clamped by the layout
        else if (hit.kind != Hit::Kind::Minimap) m_camera.zoomAt(mouse, std::pow(kZoomStep, io.MouseWheel));
    }
    if (m_canvasActive && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_camera.panBy(io.MouseDelta);

    // ---- right button: drag pans, a click opens a menu
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { m_rightDown = true; m_rightDragged = false; }
    if (m_rightDown) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            m_rightDragged = true;
            m_camera.panBy(io.MouseDelta);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            m_rightDown = false;
            if (!m_rightDragged && m_drag == Drag::None && hit.kind != Hit::Kind::Minimap) {
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
    }

    // ---- left button
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const bool dbl = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        m_press      = hit;
        m_pressMouse = mouse;
        m_pressGraph = cameraFor(hit.node).toGraph(mouse);
        m_pressCtrl  = io.KeyCtrl;
        m_pressShift = io.KeyShift;
        switch (hit.kind) {
        case Hit::Kind::Port:   startLink(hit); break;
        case Hit::Kind::Widget: pressWidget(hit, dbl); break;
        case Hit::Kind::Node:   pressNode(hit.node); break;
        case Hit::Kind::Minimap:
            m_drag = Drag::Minimap;
            m_minimap.press(mouse, m_camera, m_canvasSize);
            break;
        case Hit::Kind::Insert: {
            const GraphLink& l = m_links[size_t(hit.index)];
            PendingAdd add;
            add.mode = PendingAdd::Mode::Insert;
            add.at   = m_camera.toGraph(linkPoint(l.curve, 0.5f));
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
    const NodeShape* s = shape(hit.node);
    Node* n = m_project->find(hit.node);
    if (!s || !n) return;
    const NodeWidget w = s->widgets[size_t(hit.index)];
    m_pressWidget = w;
    bringToFront(hit.node);

    switch (w.control) {
    case NodeControl::Title:
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
    case NodeControl::Lock:
        m_project->setRasterLocked(hit.node, !static_cast<RasterRootNode*>(n)->locked());
        break;
    case NodeControl::LockAlpha:
        m_project->setRasterAlphaLocked(hit.node, !static_cast<RasterRootNode*>(n)->alphaLocked());
        break;
    case NodeControl::SectionToggle: {
        bool& e = m_viewStates[hit.node].expanded[int(w.signal)];
        e = !e;
        break;
    }
    case NodeControl::Scale:
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
        for (const NodeChannelRow& row : s->channels)   // the track drags; the value text doesn't
            if (row.signal == w.signal && row.channel == w.channel && m_pressGraph.x <= row.track.Max.x + 5.0f)
                m_drag = Drag::Scale;
        break;
    case NodeControl::OffsetX:
    case NodeControl::OffsetY: {
        const auto* m = static_cast<MoveNode*>(n);
        const int axis = w.control == NodeControl::OffsetX ? 0 : 1;
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
    case NodeControl::Layer:
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
        for (const auto& [id, s] : m_shapes) {
            if (isMaster(id) || !s.bounds.Overlaps(band)) continue;   // the master isn't in this space
            if (m_pressCtrl && !m_pressShift && m_bandBase.contains(id)) next.erase(id);   // Ctrl: toggle
            else next.insert(id);
        }
        m_selection.assign(std::move(next));
        break;
    }

    case Drag::Scale: {
        const NodeShape* s = shape(m_press.node);
        if (!s) break;
        for (const NodeChannelRow& row : s->channels)
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
        e.x    = m_pressWidget.control == NodeControl::OffsetX ? v : m->offsetX();
        e.y    = m_pressWidget.control == NodeControl::OffsetY ? v : m->offsetY();
        m_project->previewEdit(e);
        break;
    }

    case Drag::Layer: {
        const NodeShape* s = shape(m_layerDrag.node);
        if (!s) break;
        if (!m_layerDrag.lifted) {
            if (!moved) break;
            m_layerDrag.lifted = true;
        }
        // Pulled out sideways: the layer comes off the stack and is carried as a link, to drop
        // into another stack (or on nothing: it stays off).
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
    const NodePort& p = shape(hit.node)->ports[size_t(hit.index)];
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
    // Above every layer row whose middle is below y. With the gap open, the rows under it have
    // moved down by one row: the gap stays put until y passes a row's middle.
    const NodeShape* s = shape(id);
    uint32_t at = 0;
    if (s)
        for (const NodeLayerRow& row : s->layers)
            if (row.kind == NodeLayerRow::Kind::Layer && row.rect.GetCenter().y > y) ++at;
    return at;
}

GraphPanel::LinkTarget GraphPanel::linkTarget(const Hit& hit) const
{
    LinkTarget t;
    // Anywhere over a compositor (but its mask inputs): inserted into its stack, at the mouse.
    if (m_link.fromOutput && dynamic_cast<const CompositorNode*>(m_project->find(hit.node)) &&
        (hit.kind == Hit::Kind::Node || hit.kind == Hit::Kind::Widget ||
         (hit.kind == Hit::Kind::Port && !shape(hit.node)->ports[size_t(hit.index)].mask))) {
        const InputSlot slot = InputSlot::newLayer(stackPosition(hit.node, cameraFor(hit.node).toGraph(ImGui::GetIO().MousePos).y));
        if (!m_project->canConnect(m_link.node, hit.node, slot)) return t;
        const NodePort* p = shape(hit.node)->input(slot);   // the gap's port, once it is open
        return { true, hit.node, slot, p ? cameraFor(hit.node).toScreen(p->pos) : ImGui::GetIO().MousePos };
    }
    if (hit.kind == Hit::Kind::Port) {
        const NodePort& p = shape(hit.node)->ports[size_t(hit.index)];
        if (m_link.fromOutput && !p.output && m_project->canConnect(m_link.node, hit.node, p.slot))
            t = { true, hit.node, p.slot, cameraFor(hit.node).toScreen(p.pos) };
        else if (!m_link.fromOutput && p.output && m_project->canConnect(hit.node, m_link.node, m_link.slot))
            t = { true, hit.node, {}, cameraFor(hit.node).toScreen(p.pos) };
    } else if ((hit.kind == Hit::Kind::Node || hit.kind == Hit::Kind::Widget) && m_link.fromOutput) {
        // Dropped on a node's body: its natural input (a unary input, a new top layer).
        InputSlot slot;
        const Node* n = m_project->find(hit.node);
        if (n && preferredInput(*n, slot) && m_project->canConnect(m_link.node, hit.node, slot))
            if (const NodePort* p = shape(hit.node)->input(slot)) t = { true, hit.node, slot, cameraFor(hit.node).toScreen(p->pos) };
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
    case PendingAdd::Mode::Insert:     x -= kNodeWidth * 0.5f; y -= portY;     break;   // centred on the link
    }
    if (a.mode == PendingAdd::Mode::Insert) makeRoomForInsert(a, x);
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

// Blender's auto-offset: the inserted node goes right after its source, and the node it now
// feeds moves right, together with everything downstream of it, until nothing overlaps.
void GraphPanel::makeRoomForInsert(const PendingAdd& a, float& x)
{
    constexpr float kGap = 40.0f;
    const Node* from = m_project->find(a.from);
    Node* to = m_project->find(a.to);
    if (!from || !to) return;
    x = from->m_meta.x + kNodeWidth + kGap;
    if (isMaster(a.to)) return;   // pinned: nothing to push out of the way
    const float push = x + kNodeWidth + kGap - to->m_meta.x;
    if (push <= 0.0f) return;
    for (const NodePtr& n : m_project->nodes())
        if (n.get() == to || n->dependsOn(to)) m_project->setNodePosition(n->m_meta.id, n->m_meta.x + push, n->m_meta.y);
}

void GraphPanel::openValueEdit(const ValueEdit& edit)
{
    m_edit = edit;
    m_edit.justOpened = true;
    m_editPending = true;
}

void GraphPanel::handleKeys(ImVec2 canvasSize)
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) return;
    const ImGuiIO& io = ImGui::GetIO();
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;

    if (plain && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false) ||
                  (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_X, false))))
        deleteSelection();
    if (plain && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false)) frameNodes(!m_selection.empty(), canvasSize);
    if (plain && ImGui::IsKeyPressed(ImGuiKey_Home, false)) frameNodes(false, canvasSize);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        GraphSelection::Set all;
        for (const auto& [id, s] : m_shapes)
            if (!isMaster(id)) all.insert(id);
        m_selection.assign(std::move(all));
    }
    if (plain && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        PendingAdd add;
        add.at = m_camera.toGraph(io.MousePos);
        openAdd(add, NodeMenu::Fit::Any);
    }
}

void GraphPanel::deleteSelection()
{
    const std::vector<NodeId> ids(m_selection.ids().begin(), m_selection.ids().end());
    m_project->deleteNodes(ids);   // the master is skipped; the rest heal the links around them
}

void GraphPanel::frameNodes(bool selectionOnly, ImVec2 canvasSize)
{
    ImRect r;
    bool any = false;
    for (const auto& [id, s] : m_shapes) {
        if (isMaster(id) || (selectionOnly && !m_selection.contains(id))) continue;
        if (any) r.Add(s.bounds); else r = s.bounds;
        any = true;
    }
    if (!any) return;
    // Into the part the pinned master leaves free, if that leaves enough.
    ImVec2 area = canvasSize;
    if (!m_pinnedRect.IsInverted()) {
        const float covered = m_camera.origin().x + canvasSize.x - m_pinnedRect.Min.x + kPinMargin * m_uiScale;
        if (canvasSize.x - covered > canvasSize.x * 0.4f) area.x -= covered;
    }
    m_camera.frame(r.Min, r.Max, area, kFrameMargin);
}

int GraphPanel::activeWidget(NodeId id) const
{
    if (id != m_press.node || (m_drag != Drag::Scale && m_drag != Drag::Offset)) return -1;
    const NodeShape* s = shape(id);
    if (!s) return -1;
    for (size_t i = 0; i < s->widgets.size(); ++i) {
        const NodeWidget& w = s->widgets[i];
        if (w.control != m_pressWidget.control) continue;
        if (w.signal == m_pressWidget.signal && w.channel == m_pressWidget.channel) return int(i);
    }
    return -1;
}

void GraphPanel::setCursor(const Hit& hit) const
{
    ImGuiMouseCursor c = ImGuiMouseCursor_Arrow;
    if (m_rightDragged || (m_canvasActive && ImGui::IsMouseDown(ImGuiMouseButton_Middle))) c = ImGuiMouseCursor_ResizeAll;
    else if (m_drag == Drag::Scale || m_drag == Drag::Offset) c = ImGuiMouseCursor_ResizeEW;
    else if (m_drag == Drag::Layer) c = ImGuiMouseCursor_ResizeNS;
    else if (m_drag == Drag::Link) c = ImGuiMouseCursor_Hand;
    else if (m_drag == Drag::None) {
        if (hit.kind == Hit::Kind::Port || hit.kind == Hit::Kind::Insert) c = ImGuiMouseCursor_Hand;
        if (hit.kind == Hit::Kind::Widget) {
            switch (shape(hit.node)->widgets[size_t(hit.index)].control) {
            case NodeControl::Lock:
            case NodeControl::LockAlpha:
            case NodeControl::SectionToggle: c = ImGuiMouseCursor_Hand; break;
            case NodeControl::Scale:
            case NodeControl::OffsetX:
            case NodeControl::OffsetY:       c = ImGuiMouseCursor_ResizeEW; break;
            case NodeControl::Layer:         c = ImGuiMouseCursor_ResizeNS; break;
            case NodeControl::Title:         break;
            }
        }
    }
    ImGui::SetMouseCursor(c);
}

// ---------------------------------------------------------------- painting

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
        const ImU32 color = withAlpha(l.mask ? kMaskPort : kImagePort, 220);
        if (l.toMaster) paintFadingLink(g, l.curve, color, linkHovered && hit.index == int(i));
        else            paintLink(g, l.curve, color, linkHovered && hit.index == int(i));
    }

    // ---- nodes, back to front; a selection outline belongs to its node's layer
    const auto paintOne = [&](const GraphPaint& pg, NodeId id) {
        const Node* n = m_project->find(id);
        const NodeShape* s = shape(id);
        if (!n || !s) return;
        NodeVisual v;
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
        if (const NodeShape* from = shape(layer->m_meta.id); from && from->output()) {
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
    m_minimapEdges.clear();
    for (const GraphLink& l : m_links)
        if (!l.toMaster) m_minimapEdges.push_back({ l.fromGraph, l.toGraph, l.mask });
    m_minimap.paint(dl, *m_project, m_minimapEdges, m_selection.ids(), m_camera, m_canvasSize, ImGui::GetIO().MousePos,
                    m_drag == Drag::None || m_drag == Drag::Minimap);

    // ---- the link being dragged, on top
    if (m_drag == Drag::Link) {
        const NodeShape* s = shape(m_link.node);
        const NodePort* fixed = s ? (m_link.fromOutput ? s->output() : s->input(m_link.slot)) : nullptr;
        if (fixed) {
            const LinkTarget t = linkTarget(hit);
            const ImVec2 loose = t.valid ? t.pos : ImGui::GetIO().MousePos;
            const ImVec2 anchor = cameraFor(m_link.node).toScreen(fixed->pos);
            const LinkCurve c = m_link.fromOutput ? linkCurve(g, anchor, loose) : linkCurve(g, loose, anchor);
            const bool mask = t.valid && t.slot.kind == InputSlot::Kind::Mask;
            paintLink(g, c, t.valid ? (mask ? kMaskPort : kImagePort) : IM_COL32(230, 230, 230, 170), t.valid);
        }
    }

    if (linkHovered) paintInsertButton(g, linkPoint(m_links[size_t(hit.index)].curve, 0.5f), hit.kind == Hit::Kind::Insert);

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
            if (ImGui::MenuItem("Rename...")) {
                ValueEdit e;
                e.kind = ValueEdit::Kind::Rename;
                e.node = id;
                std::snprintf(e.text, sizeof e.text, "%s", n->m_meta.label.c_str());
                openValueEdit(e);
            }
            if (!master) {
                ImGui::Separator();
                const bool many = m_selection.contains(id) && m_selection.size() > 1;
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
        if (m_edit.justOpened) { ImGui::SetKeyboardFocusHere(); m_edit.justOpened = false; }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
        bool commit = false;
        switch (m_edit.kind) {
        case ValueEdit::Kind::Rename:
            if ((commit = ImGui::InputText("##name", m_edit.text, sizeof m_edit.text, kFlags)))
                m_project->setNodeLabel(m_edit.node, m_edit.text);
            break;
        case ValueEdit::Kind::Scale:
            if ((commit = ImGui::InputFloat("##scale", &m_edit.value, 1.0f, 10.0f, "%.1f", kFlags)))
                m_project->setSignalScale(m_edit.node, m_edit.signal, m_edit.channel, std::clamp(m_edit.value / 100.0f, 0.0f, 1.0f));
            break;
        case ValueEdit::Kind::Offset:
            if ((commit = ImGui::InputFloat("##offset", &m_edit.value, 1.0f, 10.0f, "%.0f px", kFlags)))
                if (const auto* m = dynamic_cast<MoveNode*>(m_project->find(m_edit.node))) {
                    const int v = int(std::lround(m_edit.value));
                    if (m_edit.axis == 0) m_project->setMoveOffset(m_edit.node, v, m->offsetY());
                    else                  m_project->setMoveOffset(m_edit.node, m->offsetX(), v);
                }
            break;
        }
        if (commit || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

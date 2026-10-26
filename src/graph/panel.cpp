#include "graph/panel.hpp"
#include "core/actions.hpp"
#include "core/input.hpp"
#include "core/project.hpp"
#include "gfx/device.hpp"
#include "graph/style.hpp"

#include <imgui.h>
#include <algorithm>

using namespace GraphStyle;

namespace {

constexpr double kThumbBudgetMs  = 3.0;    // per frame
constexpr float  kLinkHoverPx    = 6.0f;
constexpr float  kInsertRadiusPx = 10.0f;
constexpr float  kFrameMargin    = 40.0f;  // points around framed nodes

}

void GraphPanel::init(GpuDevice& gpu, Project& project, const InputFrame& input, ActionRegistry& actions)
{
    m_project = &project;
    m_input   = &input;
    m_actions = &actions;
    registerActions(actions);
    m_thumbs.init(gpu.device(), gpu.queue());
    m_subscription = project.subscribe([this](const ProjectEvent& e) {
        if (e.type != ProjectEventType::NodeRemoved) return;
        m_selection.erase(e.node);
        m_viewStates.erase(e.node);
        m_items.erase(e.node);
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
    // One item covers the canvas and owns the mouse while a button is held.
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
    // Its shortcuts while hovered, or focused with the pointer not on the canvas. Fired inside the
    // window, so what they open (the add menu) belongs to it.
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if ((hovered || (focused && !m_pointerElsewhere)) && !ImGui::GetIO().WantTextInput)
        m_actions->dispatch(*m_input, ActionScope::Graph);

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

bool GraphPanel::dropTarget(const ImGuiWindow* window, ImVec2 screen, ImVec2& nodePos) const
{
    if (!m_window || !window || window->RootWindow != m_window) return false;
    if (screen.x < m_canvasMin.x || screen.y < m_canvasMin.y ||
        screen.x >= m_canvasMin.x + m_canvasSize.x || screen.y >= m_canvasMin.y + m_canvasSize.y) return false;
    const ImVec2 g = m_camera.toGraph(screen);
    nodePos = ImVec2(g.x - kNodeWidth * 0.5f, g.y - kHeaderH * 0.5f);   // header centred on the drop
    return true;
}

void GraphPanel::selectOnly(NodeId id)
{
    m_selection.select(id);
    bringToFront(id);
}

void GraphPanel::syncNodes()
{
    // Rebuilt in place every time (cheap, and never stale); removed nodes leave via NodeRemoved.
    for (const NodePtr& n : m_project->nodes()) {
        const NodeId id = n->m_meta.id;
        StackLayout layout;
        if (id == m_layerDrag.node && m_layerDrag.lifted) layout = { m_layerDrag.at, int(m_layerDrag.layer) };
        else if (id == m_pendingLayer.node)               layout.gap = m_pendingLayer.at;
        auto [it, added] = m_items.try_emplace(id);
        buildNodeItem(*m_project, *n, m_viewStates[id], layout, it->second);
        if (added) m_zOrder.push_back(id);
    }
}

bool GraphPanel::isMaster(NodeId id) const
{
    return id != kNoNodeId && m_project->isMaster(id);
}

void GraphPanel::layoutOverlays(ImVec2 canvasMin, ImVec2 canvasMax)
{
    // The master's camera: 100%, its node item's top-right corner at the panel's (less a margin),
    // scrolled when it is taller than the panel.
    const NodeId master = m_project->master().m_meta.id;
    m_pinned.zoom = 1.0f;
    m_pinned.setFrame(canvasMin, m_uiScale);
    m_pinnedRect = ImRect(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
    if (const NodeItem* s = item(master)) {
        const float k = m_pinned.scale(), margin = kPinMargin * m_uiScale;
        const float maxScroll = std::max(0.0f, s->bounds.GetHeight() * k + 2.0f * margin - (canvasMax.y - canvasMin.y));
        m_pinnedScroll = std::clamp(m_pinnedScroll, 0.0f, maxScroll);
        m_pinned.position = ImVec2(s->bounds.Max.x - (canvasMax.x - margin - canvasMin.x) / k,
                                   s->bounds.Min.y - (margin - m_pinnedScroll) / k);
        m_pinnedRect = ImRect(m_pinned.toScreen(s->bounds.Min), m_pinned.toScreen(s->bounds.Max));
    }
    m_minimap.layout(m_items, master, canvasMin, canvasMax, m_pinnedRect, m_uiScale);
}

void GraphPanel::rebuildLinks(const GraphPaint& g)
{
    m_links.clear();
    for (const NodePtr& n : m_project->nodes()) {
        const NodeId to = n->m_meta.id;
        const NodeItem* dst = item(to);
        for (const NodeItemPort& p : dst->ports) {
            if (p.output) continue;
            const NodePtr src = m_project->inputAt(to, p.slot);
            const NodeItem* from = src ? item(src->m_meta.id) : nullptr;
            const NodeItemPort* out = from ? from->output() : nullptr;
            if (!out) continue;
            const ImVec2 a = cameraFor(src->m_meta.id).toScreen(out->pos), b = cameraFor(to).toScreen(p.pos);
            m_links.push_back({ src->m_meta.id, to, p.slot, p.mask, isMaster(to), linkCurve(g, a, b), out->pos, p.pos });
        }
    }
}

const NodeItem* GraphPanel::item(NodeId id) const
{
    auto it = m_items.find(id);
    return it == m_items.end() ? nullptr : &it->second;
}

void GraphPanel::bringToFront(NodeId id)
{
    std::erase(m_zOrder, id);
    m_zOrder.push_back(id);
}

GraphPanel::Hit GraphPanel::hitNode(NodeId id, const NodeItem& s, ImVec2 p, float portR) const
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
    for (const NodeItemBox& b : s.boxes)
        if (b.rect.Contains(p)) return { Hit::Kind::Node, id, -1 };
    return {};
}

GraphPanel::Hit GraphPanel::hitTest(ImVec2 mouse, const GraphPaint& g) const
{
    // The pinned master and the navigator float over everything.
    const NodeId master = m_project->master().m_meta.id;
    if (const NodeItem* s = item(master)) {
        const Hit h = hitNode(master, *s, m_pinned.toGraph(mouse), std::max(kPortHit, g.px(7.0f) / m_pinned.scale()));
        if (h.kind != Hit::Kind::None) return h;
    }
    if (m_minimap.contains(mouse)) return { Hit::Kind::Minimap, kNoNodeId, -1 };

    const ImVec2 p = m_camera.toGraph(mouse);
    const float portR = std::max(kPortHit, g.px(7.0f) / g.scale());
    for (auto it = m_zOrder.rbegin(); it != m_zOrder.rend(); ++it) {   // front to back
        const NodeItem* s = item(*it);
        if (!s || *it == master) continue;
        const Hit h = hitNode(*it, *s, p, portR);
        if (h.kind != Hit::Kind::None) return h;
    }

    // The insert button of the link hovered last frame sits on top of every link.
    if (m_hoveredLink >= 0 && m_hoveredLink < int(m_links.size())) {
        const ImVec2 at = insertPoint(m_links[size_t(m_hoveredLink)], mouse);
        const float dx = mouse.x - at.x, dy = mouse.y - at.y;
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

void GraphPanel::registerActions(ActionRegistry& actions)
{
    const auto graph = ActionScope::Graph;
    actions.add({ .id = "graph.delete", .title = "Delete Selected",
                  .shortcuts = { { SDLK_DELETE }, { SDLK_BACKSPACE } }, .scope = graph,
                  .enabled = [this] { return !m_selection.empty(); }, .run = [this] { deleteSelection(); } });
    actions.add({ .id = "graph.frameSelected", .title = "Frame Selected", .shortcuts = { { SDLK_F } }, .scope = graph,
                  .run = [this] { frameNodes(!m_selection.empty(), m_canvasSize); } });
    actions.add({ .id = "graph.frameAll", .title = "Frame All", .shortcuts = { { SDLK_HOME } }, .scope = graph,
                  .run = [this] { frameNodes(false, m_canvasSize); } });
    actions.add({ .id = "graph.selectAll", .title = "Select All", .shortcuts = { { SDLK_A, ModPrimary } }, .scope = graph,
                  .run = [this] {
                      GraphSelection::Set all;
                      for (const auto& [id, s] : m_items)
                          if (!isMaster(id)) all.insert(id);
                      m_selection.assign(std::move(all));
                  } });
    actions.add({ .id = "graph.addNode", .title = "Add Node...", .shortcuts = { { SDLK_A, ModShift } }, .scope = graph,
                  .run = [this] {
                      PendingAdd add;
                      add.at = m_camera.toGraph(ImGui::GetIO().MousePos);
                      openAdd(add, NodeMenu::Fit::Any);
                  } });
}

void GraphPanel::showInCanvas(const std::vector<NodeId>& ids)
{
    BoundsI b;
    for (NodeId id : ids)
        if (Node* n = m_project->find(id)) b.expand(n->bounds());   // a Move: where it moved them to
    m_canvasFrame = b;
}

bool GraphPanel::takeCanvasFrame(BoundsI& out)
{
    if (!m_canvasFrame) return false;
    out = *m_canvasFrame;
    m_canvasFrame.reset();
    return true;
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
    for (const auto& [id, s] : m_items) {
        if (isMaster(id) || (selectionOnly && !m_selection.contains(id))) continue;
        if (any) r.Add(s.bounds); else r = s.bounds;
        any = true;
    }
    // Into the part the pinned master leaves free, if that leaves enough.
    ImVec2 area = canvasSize;
    if (!m_pinnedRect.IsInverted()) {
        const float covered = m_camera.origin().x + canvasSize.x - m_pinnedRect.Min.x + kPinMargin * m_uiScale;
        if (canvasSize.x - covered > canvasSize.x * 0.4f) area.x -= covered;
    }
    if (any) m_camera.frame(r.Min, r.Max, area, kFrameMargin);
    else     m_camera.centerOn(ImVec2(0.0f, 0.0f), area);   // nothing to frame: back to the origin
}

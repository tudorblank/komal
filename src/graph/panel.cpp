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
    m_subscription = project.subscribe([this](const ProjectEvent& e) { onProjectEvent(e); });
    // The ones made before we listened: every item first, then the links between them.
    for (const NodePtr& n : project.nodes()) addNodeItem(n->m_meta.id, false);
    for (const NodePtr& n : project.nodes()) addLinksInto(n->m_meta.id);
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

    layoutOverlays(canvasMin, canvasMax);
    if (m_frameOnStart > 0) {
        frameNodes(false, size);
        --m_frameOnStart;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const GraphPaint g = paintContext(dl);
    placeLinkCurves(g);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool tracking = hovered || m_drag != Drag::None;
    Hit hit = tracking ? hitTest(mouse, g) : Hit{};
    handleMouse(hit, hovered);
    // Its shortcuts next frame: the pointer over it, or focused (a panel with the pointer wins).
    if (hovered) m_actions->claimKeyboard(ActionScope::Graph, KeyboardClaim::Pointer);
    else if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) m_actions->claimKeyboard(ActionScope::Graph, KeyboardClaim::Focus);

    // Input changed the project through its calls, and the items through its events; a drag
    // may have moved a gap in a stack.
    updateStackGaps();
    layoutOverlays(canvasMin, canvasMax);
    placeLinkCurves(g);
    hit = tracking ? hitTest(mouse, g) : Hit{};
    m_hoveredLink = (hit.kind == Hit::Kind::Link || hit.kind == Hit::Kind::Insert) ? hit.index : -1;
    // A link dragged over a stack opens a gap where it would land (laid out from next frame on).
    m_pendingLayer = {};
    if (m_drag == Drag::Link)
        if (const LinkTarget t = linkTarget(hit); t.valid && t.slot.kind == InputSlot::Kind::NewLayer)
            m_pendingLayer = { t.node, int(t.slot.layer) };

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

// ---------------------------------------------------------------- items

void GraphPanel::onProjectEvent(const ProjectEvent& e)
{
    switch (e.type) {
    case ProjectEventType::NodeAdded:     addNodeItem(e.node); break;
    case ProjectEventType::NodeRemoved:   removeNodeItem(e.node); break;
    case ProjectEventType::NodeMoved:     moveNodeItem(e.node); break;
    case ProjectEventType::LinkAdded:     addLink(e.source, e.node, e.slot); break;
    case ProjectEventType::LinkRemoved:   removeLink(e.node, e.slot); break;
    case ProjectEventType::LayersChanged:
        layoutNodeItem(e.node);
        addLinksInto(e.node, true);
        break;
    default:   // values, pixels, previews, history: painted as they are
        break;
    }
}

void GraphPanel::addNodeItem(NodeId id, bool withLinks)
{
    const Node* n = m_project->find(id);
    if (!n || m_items.contains(id)) return;
    buildNodeItem(*m_project, *n, m_viewStates[id], {}, m_items[id]);
    m_zOrder.push_back(id);
    ++m_itemsVersion;
    if (withLinks) addLinksInto(id);   // made already wired (a reference)
}

void GraphPanel::removeNodeItem(NodeId id)
{
    m_selection.erase(id);
    m_viewStates.erase(id);
    m_items.erase(id);
    m_thumbs.forget(id);
    std::erase(m_zOrder, id);
    std::erase(m_gapped, id);
    std::erase_if(m_links, [&](const GraphLink& l) { return l.from == id || l.to == id; });
    m_linksMoved = true;
    ++m_itemsVersion;
}

void GraphPanel::moveNodeItem(NodeId id)
{
    auto it = m_items.find(id);
    const Node* n = m_project->find(id);
    if (it == m_items.end() || !n) return;
    it->second.moveTo(n->m_meta.x, n->m_meta.y);
    placeLinkEndsOf(id);
    ++m_itemsVersion;
}

void GraphPanel::layoutNodeItem(NodeId id)
{
    auto it = m_items.find(id);
    const Node* n = m_project->find(id);
    if (it == m_items.end() || !n) return;
    const StackLayout stack = stackLayout(id);
    buildNodeItem(*m_project, *n, m_viewStates[id], stack, it->second);
    std::erase(m_gapped, id);
    if (it->second.stack != StackLayout{}) m_gapped.push_back(id);
    placeLinkEndsOf(id);
    ++m_itemsVersion;
}

void GraphPanel::addLink(NodeId from, NodeId to, const InputSlot& slot)
{
    std::erase_if(m_links, [&](const GraphLink& l) { return l.to == to && l.slot == slot; });   // a slot has one source
    const bool mask = slot.kind == InputSlot::Kind::Mask;
    if (mask) layoutNodeItem(to);   // a collapsed section shows every masked channel
    GraphLink l{ from, to, slot, mask, isMaster(to), {}, {}, {} };
    placeLinkEnds(l);
    m_links.push_back(l);
    m_linksMoved = true;
}

void GraphPanel::removeLink(NodeId to, const InputSlot& slot)
{
    std::erase_if(m_links, [&](const GraphLink& l) { return l.to == to && l.slot == slot; });
    if (slot.kind == InputSlot::Kind::Mask) layoutNodeItem(to);
    m_linksMoved = true;
}

void GraphPanel::addLinksInto(NodeId to, bool layersOnly)
{
    const Node* n = m_project->find(to);
    if (!n) return;
    std::erase_if(m_links, [&](const GraphLink& l) { return l.to == to && (!layersOnly || l.slot.kind == InputSlot::Kind::Layer); });
    m_linksMoved = true;
    if (auto* c = dynamic_cast<const CompositorNode*>(n))
        for (uint32_t i = 0; i < uint32_t(c->layers().size()); ++i) addLink(c->layers()[i]->m_meta.id, to, InputSlot::stackLayer(i));
    if (layersOnly) return;
    if (auto* u = dynamic_cast<const UnaryNode*>(n); u && u->input()) addLink(u->input()->m_meta.id, to, InputSlot::input());
    for (int s = 0; s < kSignalCount; ++s)
        for (int ch = 0; ch < kChannelCount; ++ch)
            if (const NodePtr& m = n->signal(NodeSignal(s)).mask[ch])
                addLink(m->m_meta.id, to, InputSlot::mask(NodeSignal(s), NodeChannel(ch)));
}

void GraphPanel::placeLinkEnds(GraphLink& l) const
{
    const NodeItem* from = item(l.from);
    const NodeItem* to   = item(l.to);
    const NodeItemPort* out = from ? from->output() : nullptr;
    const NodeItemPort* in  = to ? to->input(l.slot) : nullptr;
    l.shown = out && in;
    if (out) l.fromGraph = out->pos;
    if (in)  l.toGraph   = in->pos;
}

void GraphPanel::placeLinkEndsOf(NodeId id)
{
    for (GraphLink& l : m_links)
        if (l.from == id || l.to == id) placeLinkEnds(l);
    m_linksMoved = true;
}

StackLayout GraphPanel::stackLayout(NodeId id) const
{
    if (!dynamic_cast<const CompositorNode*>(m_project->find(id))) return {};
    if (id == m_layerDrag.node && m_layerDrag.lifted) return { m_layerDrag.at, int(m_layerDrag.layer) };
    if (id == m_pendingLayer.node)                    return { m_pendingLayer.at, -1 };
    return {};
}

void GraphPanel::updateStackGaps()
{
    const auto check = [&](NodeId id) {
        const NodeItem* s = item(id);
        if (s && s->stack != stackLayout(id)) layoutNodeItem(id);
    };
    for (NodeId id : std::vector<NodeId>(m_gapped)) check(id);   // copy: laying out edits it
    check(m_layerDrag.node);
    check(m_pendingLayer.node);
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
    const std::array<float, 9> key{ canvasMin.x, canvasMin.y, canvasMax.x, canvasMax.y,
                                    m_pinnedRect.Min.x, m_pinnedRect.Min.y, m_pinnedRect.Max.x, m_pinnedRect.Max.y, m_uiScale };
    if (key != m_minimapKey || m_itemsVersion != m_minimapVersion) {   // fitted to the items: only when they or its room changed
        m_minimapKey     = key;
        m_minimapVersion = m_itemsVersion;
        m_minimap.layout(m_items, master, canvasMin, canvasMax, m_pinnedRect, m_uiScale);
    }
}

void GraphPanel::placeLinkCurves(const GraphPaint& g)
{
    // Curves are screen space: placed again when a link or either camera moved.
    const ImVec2 a = m_camera.toScreen(ImVec2(0, 0)), b = m_pinned.toScreen(ImVec2(0, 0));
    const std::array<float, 6> cameras{ a.x, a.y, m_camera.scale(), b.x, b.y, m_uiScale };
    if (!m_linksMoved && cameras == m_linkCameras) return;
    if (m_linksMoved) {
        m_minimapEdges.clear();
        for (const GraphLink& l : m_links)
            if (l.shown && !l.toMaster) m_minimapEdges.push_back({ l.fromGraph, l.toGraph, l.mask });
    }
    m_linksMoved  = false;
    m_linkCameras = cameras;
    for (GraphLink& l : m_links)
        if (l.shown) l.curve = linkCurve(g, cameraFor(l.from).toScreen(l.fromGraph), cameraFor(l.to).toScreen(l.toGraph));
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
        if (!m_links[i].shown) continue;
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

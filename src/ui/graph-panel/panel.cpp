#include "ui/graph-panel/panel.hpp"
#include "command/actions.hpp"
#include "command/input.hpp"
#include "base/perf.hpp"
#include "canvas/canvas-engine.hpp"
#include "document/document-actions.hpp"
#include "document/project.hpp"
#include "graph/style.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <algorithm>
#include <cstdio>

using namespace GraphStyle;

namespace {

constexpr float  kFrameMargin    = 40.0f;  // points around framed nodes

}

PanelInfo GraphPanel::info() const { return { Actions::ViewGraph, "Graph", DockSlot::Bottom }; }

bool GraphPanel::init()
{
    const Project& project = ui().project;
    m_scene.init(project);
    m_menus.init(project, ui().actions);
    registerActions(ui().actions);
    return true;
}

void GraphPanel::shutdown()
{
    m_scene.shutdown();
}

void GraphPanel::draw()
{
    PerfScope perf(Perf::Graph);
    m_surface.hide();
    if (!isOpen()) return;
    const Widgets::PanelWindow window(*this, ui().actions, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse,
                                      Widgets::PanelWindow::Padding::None);
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (!window || size.x < 1.0f || size.y < 1.0f) return;

    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    m_surface.show(ImGui::GetCurrentWindowRead(), canvasMin, size);
    const ImVec2 canvasMax(canvasMin.x + size.x, canvasMin.y + size.y);
    // One item covers the canvas and owns the mouse while a button is held.
    ImGui::InvisibleButton("##graph", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                            ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    m_canvasActive = ImGui::IsItemActive();
    m_camera.setFrame(canvasMin, uiScale());

    layoutOverlays(canvasMin, canvasMax);
    if (m_autoFrame && (size.x != m_framedSize.x || size.y != m_framedSize.y || m_scene.itemsRevision() != m_framedItems)) {
        frameNodes(false, size);
        m_framedSize  = size;
        m_framedItems = m_scene.itemsRevision();
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const GraphPaint g = paintContext(dl, m_camera);
    placeLinkCurves(g);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool tracking = hovered || dragging();
    GraphHit hit = tracking ? hitAt(mouse) : GraphHit{};
    handleMouse(hit, hovered);
    // Its shortcuts next frame: the pointer over it, or focused (a panel with the pointer wins).
    if (hovered) ui().actions.claimKeyboard(ActionScope::Graph, KeyboardClaim::Pointer);
    else if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) ui().actions.claimKeyboard(ActionScope::Graph, KeyboardClaim::Focus);

    // Input changed the project through its calls, and the scene through its events; a drag
    // may have moved a gap in a stack.
    updateStackGap();
    layoutOverlays(canvasMin, canvasMax);
    placeLinkCurves(g);
    hit = tracking ? hitAt(mouse) : GraphHit{};
    m_hoveredLink = hit.link();
    // A link dragged over a stack opens a gap where it would land (laid out from next frame on).
    if (auto* link = std::get_if<LinkDrag>(&m_drag)) {
        const LinkTarget t = linkTarget(*link, hit);
        const auto* gap = t.valid ? t.slot.as<InputSlot::NewLayer>() : nullptr;
        link->gapStack = gap ? t.node : kNoNodeId;
        link->gapAt    = gap ? int(gap->at) : -1;
    }

    dl->PushClipRect(canvasMin, canvasMax, true);
    paint(g, hit, canvasMin, canvasMax);
    dl->PopClipRect();

    m_menus.draw();
}

bool GraphPanel::dropTarget(const ImGuiWindow* window, ImVec2 screen, ImVec2& nodePos) const
{
    if (!m_surface.contains(window, screen)) return false;
    const ImVec2 g = m_camera.toGraph(screen);
    nodePos = ImVec2(g.x - kNodeWidth * 0.5f, g.y - kHeaderH * 0.5f);   // header centred on the drop
    return true;
}

const NodeSelection& GraphPanel::selection() const
{
    return ui().project.selection();
}

void GraphPanel::select(const NodeSelection& next)
{
    if (next == selection()) return;
    ui().actions.run(Actions::SelectionSet, selectionArgs(next));
}

void GraphPanel::updateStackGap()
{
    // A lifted layer leaves a gap where it would land; so does a link dragged over a stack.
    const auto* layer = std::get_if<LayerDrag>(&m_drag);
    const auto* link  = std::get_if<LinkDrag>(&m_drag);
    if (layer && layer->lifted)                   m_scene.setStackGap(layer->node, { layer->at, int(layer->layer) });
    else if (link && link->gapStack != kNoNodeId) m_scene.setStackGap(link->gapStack, { link->gapAt, -1 });
    else                                          m_scene.setStackGap(kNoNodeId, {});
}

void GraphPanel::layoutOverlays(ImVec2 canvasMin, ImVec2 canvasMax)
{
    // The master's camera: 100%, its node item's top-right corner at the panel's (less a margin),
    // scrolled when it is taller than the panel.
    const NodeId master = ui().project.master().id();
    m_pinned.zoom = 1.0f;
    m_pinned.setFrame(canvasMin, uiScale());
    m_pinnedRect = ImRect(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
    if (const NodeItem* s = m_scene.item(master)) {
        const float k = m_pinned.scale(), margin = kPinMargin * uiScale();
        const float maxScroll = std::max(0.0f, s->bounds.GetHeight() * k + 2.0f * margin - (canvasMax.y - canvasMin.y));
        m_pinnedScroll = std::clamp(m_pinnedScroll, 0.0f, maxScroll);
        m_pinned.position = ImVec2(s->bounds.Max.x - (canvasMax.x - margin - canvasMin.x) / k,
                                   s->bounds.Min.y - (margin - m_pinnedScroll) / k);
        m_pinnedRect = ImRect(m_pinned.toScreen(s->bounds.Min), m_pinned.toScreen(s->bounds.Max));
    }
    const std::array<float, 9> key{ canvasMin.x, canvasMin.y, canvasMax.x, canvasMax.y,
                                    m_pinnedRect.Min.x, m_pinnedRect.Min.y, m_pinnedRect.Max.x, m_pinnedRect.Max.y, uiScale() };
    if (key != m_minimapKey || m_scene.itemsRevision() != m_minimapRevision) {   // fitted to the items: only when they or its room changed
        m_minimapKey      = key;
        m_minimapRevision = m_scene.itemsRevision();
        m_minimap.layout(m_scene.items(), master, canvasMin, canvasMax, m_pinnedRect, uiScale());
    }
}

void GraphPanel::placeLinkCurves(const GraphPaint& g)
{
    // Curves are screen space: placed again when a link or either camera moved.
    const ImVec2 a = m_camera.toScreen(ImVec2(0, 0)), b = m_pinned.toScreen(ImVec2(0, 0));
    const std::array<float, 6> cameras{ a.x, a.y, m_camera.scale(), b.x, b.y, uiScale() };
    const bool linksMoved = m_scene.linksRevision() != m_curvesRevision;
    if (!linksMoved && cameras == m_linkCameras) return;
    const std::vector<GraphLink>& links = m_scene.links();
    if (linksMoved) {
        m_minimapEdges.clear();
        for (const GraphLink& l : links)
            if (l.shown && !l.toMaster) m_minimapEdges.push_back({ l.fromGraph, l.toGraph, l.mask });
    }
    m_curvesRevision = m_scene.linksRevision();
    m_linkCameras    = cameras;
    m_curves.resize(links.size());
    for (size_t i = 0; i < links.size(); ++i)
        if (links[i].shown) m_curves[i] = linkCurve(g, cameraFor(links[i].from).toScreen(links[i].fromGraph), cameraFor(links[i].to).toScreen(links[i].toGraph));
}

GraphHit GraphPanel::hitAt(ImVec2 mouse) const
{
    const bool hovering = m_hoveredLink >= 0 && m_hoveredLink < int(m_curves.size());
    const HitView view{ m_scene, ui().project.master().id(), m_camera, m_pinned, m_minimap, m_curves, uiScale(),
                        m_hoveredLink, hovering ? insertPoint(m_curves[size_t(m_hoveredLink)], mouse) : ImVec2() };
    return hitTest(view, mouse);
}

void GraphPanel::registerActions(ActionRegistry& actions)
{
    // Interface actions: framing, selecting, opening the add menu. Delete runs node.delete (logged).
    const auto graph = ActionScope::Graph;
    const auto ui    = ActionKind::Interface;
    actions.add({ .id = Actions::GraphDelete, .title = "Delete Selected", .kind = ui,
                  .shortcuts = { { SDLK_DELETE }, { SDLK_BACKSPACE } }, .scope = graph,
                  .enabled = [this] { return !selection().empty(); }, .run = [this] { deleteSelection(); } });
    actions.add({ .id = Actions::GraphFrameSelected, .title = "Frame Selected", .kind = ui, .shortcuts = { { SDLK_F } }, .scope = graph,
                  .run = [this] { m_autoFrame = false; frameNodes(!selection().empty(), m_surface.size()); } });
    actions.add({ .id = Actions::GraphFrameAll, .title = "Frame All", .kind = ui, .shortcuts = { { SDLK_HOME } }, .scope = graph,
                  .run = [this] { m_autoFrame = false; frameNodes(false, m_surface.size()); } });
    actions.add({ .id = Actions::GraphSelectAll, .title = "Select All", .kind = ui, .shortcuts = { { SDLK_A, ModPrimary } }, .scope = graph,
                  .run = [this] {
                      NodeSelection::Set all;
                      for (const auto& [id, s] : m_scene.items())
                          if (!m_scene.isMaster(id)) all.insert(id);
                      select(selection().withIds(std::move(all)));
                  } });
    actions.add({ .id = Actions::GraphAddNode, .title = "Add Node...", .kind = ui, .shortcuts = { { SDLK_A, ModShift } }, .scope = graph,
                  .run = [this] { m_menus.openAdd(GraphMenus::Free{}, m_camera.toGraph(ImGui::GetIO().MousePos)); } });
    // nodes: the ones to show (none given: the selection), where their output is on the canvas.
    actions.add({ .id = Actions::GraphShowInCanvas, .title = "Show in Canvas", .kind = ui, .args = { Arg::ids("nodes").optional() },
                  .mouse = "Double-click", .scope = graph,
                  .run = [this](const ActionArgs& a) {
                      std::vector<NodeId> ids;
                      if (a.has("nodes")) for (int64_t id : a.list("nodes")) ids.push_back(NodeId(id));
                      else ids = selection().list();
                      showInCanvas(ids);
                  } });
    // node: the one to rename (none given: the one selected), in a field at the mouse.
    actions.add({ .id = Actions::GraphRename, .title = "Rename...", .kind = ui, .args = { Arg::id("node").optional() }, .scope = graph,
                  .run = [this](const ActionArgs& a) {
                      const NodeId id = a.has("node") ? NodeId(a.integer("node")) : selection().single();
                      return m_menus.openValue(GraphMenus::Rename{ id });
                  } });
}

void GraphPanel::showInCanvas(const std::vector<NodeId>& ids)
{
    BoundsI b;
    for (NodeId id : ids)
        if (const Node* n = ui().project.find(id)) b.expand(n->bounds());   // a Move: where it moved them to
    ui().actions.run(Actions::CanvasFrame, { { "minX", b.minX }, { "minY", b.minY }, { "maxX", b.maxX }, { "maxY", b.maxY } });
}

void GraphPanel::deleteSelection()
{
    ui().actions.run(Actions::NodeDelete, { { "nodes", selection().list() } });   // the master is skipped; the rest heal the links around them
}

void GraphPanel::frameNodes(bool selectionOnly, ImVec2 canvasSize)
{
    ImRect r;
    bool any = false;
    for (const auto& [id, s] : m_scene.items()) {
        if (m_scene.isMaster(id) || (selectionOnly && !selection().contains(id))) continue;
        if (any) r.Add(s.bounds); else r = s.bounds;
        any = true;
    }
    // Into the part the pinned master leaves free, if that leaves enough.
    ImVec2 area = canvasSize;
    if (!m_pinnedRect.IsInverted()) {
        const float covered = m_camera.origin().x + canvasSize.x - m_pinnedRect.Min.x + kPinMargin * uiScale();
        if (canvasSize.x - covered > canvasSize.x * 0.4f) area.x -= covered;
    }
    if (any) m_camera.frame(r.Min, r.Max, area, kFrameMargin);
    else     m_camera.centerOn(ImVec2(0.0f, 0.0f), area);   // nothing to frame: back to the origin
}

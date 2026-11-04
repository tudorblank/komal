#include "ui/panels/graph/graph-panel.hpp"
#include "ui/input-owner.hpp"
#include "command/actions.hpp"
#include "input/input-frame.hpp"
#include "base/perf.hpp"
#include "ui/panels/canvas/canvas-engine.hpp"
#include "document/document-actions.hpp"
#include "document/workspace.hpp"
#include "ui/panels/graph/internal/layout.hpp"
#include "ui/style/graph-style.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <algorithm>
#include <cstdio>

using namespace GraphLayout;
using namespace GraphStyle;

namespace {

constexpr float  kFrameMargin    = 40.0f;  // points around framed nodes

}

PanelInfo GraphPanel::info() const { return { Actions::ViewGraph, "Graph", DockSlot::Bottom }; }

bool GraphPanel::init()
{
    show(ui().documents.active().id());
    registerActions(ui().actions);
    return true;
}

void GraphPanel::shutdown()
{
    m_view = nullptr;
    m_views.clear();
}

void GraphPanel::documentsChanged(const DocumentsEvent& e)
{
    switch (e.type) {
    case DocumentsEvent::Type::Opened:
        break;   // its graph is made once it's shown
    case DocumentsEvent::Type::Closing:
        if (m_view && m_view->document == e.id) m_view = nullptr;
        std::erase_if(m_views, [&](const auto& v) { return v->document == e.id; });
        break;
    case DocumentsEvent::Type::Activated:
        show(e.id);
        break;
    }
}

void GraphPanel::show(DocumentId id)
{
    // What the last one's graph left: no drag or press goes on into another (the documents only
    // switch with no edit in progress), nothing derived from its scene is kept.
    m_press = {};
    m_drag = {};
    m_hoveredLink = -1;
    m_rightDown = m_rightZooming = false;
    m_curves.clear();
    m_curvesRevision = 0;
    m_linkCameras = {};
    m_minimapEdges.clear();
    m_minimapKey = {};
    m_minimapRevision = 0;
    m_menus = GraphMenus{};
    m_menus.init(ui().workspace(), ui().actions);

    for (const auto& v : m_views)
        if (v->document == id) {
            m_view = v.get();
            return;
        }
    auto made = std::make_unique<GraphView>();
    made->document = id;
    made->scene.init(ui().workspace());
    m_view = made.get();
    m_views.push_back(std::move(made));
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
    camera().setFrame(canvasMin, uiScale());

    layoutOverlays(canvasMin, canvasMax);
    if (m_view->autoFrame && (size.x != m_view->framedSize.x || size.y != m_view->framedSize.y || scene().itemsRevision() != m_view->framedItems)) {
        frameNodes(false, size);
        m_view->framedSize  = size;
        m_view->framedItems = scene().itemsRevision();
    }
    const GraphPaint g = paintContext(camera());
    placeLinkCurves(g);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool tracking = hovered || dragging();
    GraphHit hit = tracking ? hitAt(mouse) : GraphHit{};
    handleMouse(hit, hovered);
    // Its shortcuts next frame: the pointer over it, or focused (a panel with the pointer wins).
    if (hovered) ui().owner.claim(ActionScope::Graph, InputOwner::Claim::Pointer);
    else if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) ui().owner.claim(ActionScope::Graph, InputOwner::Claim::Focus);

    // Input changed the workspace through its calls, and the scene through its events; a drag
    // may have moved a gap in a stack.
    updateStackGap();
    layoutOverlays(canvasMin, canvasMax);
    placeLinkCurves(g);
    hit = tracking ? hitAt(mouse) : GraphHit{};
    m_hoveredLink = hit.link();
    // A link dragged over a stack opens a gap where it would land (laid out from next frame on).
    if (auto* link = std::get_if<LinkDrag>(&m_drag)) {
        const LinkTarget t = linkTarget(*link, hit);
        const bool placed = t.valid && t.place.dropping();
        link->gapStack = placed ? t.node : kNoNodeId;
        link->gap      = placed ? t.place : StackGap{};
    }

    {
        const Painter::Clip clip = g.draw.clip(canvasMin, canvasMax);
        paint(g, hit, canvasMin, canvasMax);
    }

    m_menus.draw();
}

bool GraphPanel::dropTarget(const ImGuiWindow* window, ImVec2 screen, ImVec2& nodePos) const
{
    if (!m_surface.contains(window, screen)) return false;
    const ImVec2 g = camera().toGraph(screen);
    nodePos = ImVec2(g.x - kNodeWidth * 0.5f, g.y - kHeaderH * 0.5f);   // header centred on the drop
    return true;
}

const NodeSelection& GraphPanel::selection() const
{
    return ui().workspace().selection();
}

void GraphPanel::select(const NodeSelection& next)
{
    if (!next.empty()) scene().pickRows(kNoNodeId, {}, {});   // one kind of thing picked at a time: nodes, so no rows
    if (next == selection()) return;
    ui().actions.run(Actions::SelectionSet, selectionArgs(next));
}

bool GraphPanel::rowsPicked(NodeId* id) const
{
    for (const auto& [node, item] : scene().items())
        if (const NodeItemState* view = scene().viewState(node); view && !view->selected.empty()) {
            if (id) *id = node;
            return true;
        }
    return false;
}

void GraphPanel::removePickedRows()
{
    NodeId id = kNoNodeId;
    if (!rowsPicked(&id)) return;
    std::vector<NodeId> layers;
    std::vector<ArtboardId> artboards;
    for (const StackRowRef& r : scene().viewState(id)->selected) {
        if (r.layer != kNoNodeId) layers.push_back(r.layer);
        else                      artboards.push_back(r.artboard);
    }
    ActionArgs args{ { DocArgs::kNode, id }, { DocArgs::kLayers, layers } };
    if (!artboards.empty()) args.add({ DocArgs::kArtboards, artboards });
    if (ui().actions.run(Actions::LayersRemove, args)) scene().pickRows(kNoNodeId, {}, {});
}

void GraphPanel::updateStackGap()
{
    // A lifted layer leaves a gap where it would land; so does a link dragged over a stack.
    const auto* layer = std::get_if<LayerDrag>(&m_drag);
    const auto* link  = std::get_if<LinkDrag>(&m_drag);
    if (layer && layer->lifted)
        scene().setStackGap(layer->node, { layer->at, layer->rows });
    else if (link && link->gapStack != kNoNodeId)
        scene().setStackGap(link->gapStack, { link->gap, {} });
    else
        scene().setStackGap(kNoNodeId, {});
}

void GraphPanel::layoutOverlays(ImVec2 canvasMin, ImVec2 canvasMax)
{
    // The master's camera: 100%, its node item's top-right corner at the panel's (less a margin),
    // scrolled when it is taller than the panel.
    const NodeId master = ui().workspace().master().id();
    m_pinned.zoom = 1.0f;
    m_pinned.setFrame(canvasMin, uiScale());
    m_pinnedRect = ImRect(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
    if (const NodeItem* s = scene().item(master)) {
        const float k = m_pinned.scale(), margin = kPinMargin * uiScale();
        const float maxScroll = std::max(0.0f, s->bounds.GetHeight() * k + 2.0f * margin - (canvasMax.y - canvasMin.y));
        m_view->pinnedScroll = std::clamp(m_view->pinnedScroll, 0.0f, maxScroll);
        m_pinned.position = ImVec2(s->bounds.Max.x - (canvasMax.x - margin - canvasMin.x) / k,
                                   s->bounds.Min.y - (margin - m_view->pinnedScroll) / k);
        m_pinnedRect = ImRect(m_pinned.toScreen(s->bounds.Min), m_pinned.toScreen(s->bounds.Max));
    }
    const std::array<float, 9> key{ canvasMin.x, canvasMin.y, canvasMax.x, canvasMax.y,
                                    m_pinnedRect.Min.x, m_pinnedRect.Min.y, m_pinnedRect.Max.x, m_pinnedRect.Max.y, uiScale() };
    if (key != m_minimapKey || scene().itemsRevision() != m_minimapRevision) {   // fitted to the items: only when they or its room changed
        m_minimapKey      = key;
        m_minimapRevision = scene().itemsRevision();
        m_minimap.layout(scene().items(), master, canvasMin, canvasMax, m_pinnedRect, uiScale());
    }
}

void GraphPanel::placeLinkCurves(const GraphPaint& g)
{
    // Curves are screen space: placed again when a link or either camera moved.
    const ImVec2 a = camera().toScreen(ImVec2(0, 0)), b = m_pinned.toScreen(ImVec2(0, 0));
    const std::array<float, 6> cameras{ a.x, a.y, camera().scale(), b.x, b.y, uiScale() };
    const bool linksMoved = scene().linksRevision() != m_curvesRevision;
    if (!linksMoved && cameras == m_linkCameras) return;
    const std::vector<GraphLink>& links = scene().links();
    if (linksMoved) {
        m_minimapEdges.clear();
        for (const GraphLink& l : links)
            if (l.shown && !l.toMaster) m_minimapEdges.push_back({ l.fromGraph, l.toGraph, l.mask });
    }
    m_curvesRevision = scene().linksRevision();
    m_linkCameras    = cameras;
    m_curves.resize(links.size());
    for (size_t i = 0; i < links.size(); ++i)
        if (links[i].shown) m_curves[i] = linkCurve(g, cameraFor(links[i].from).toScreen(links[i].fromGraph), cameraFor(links[i].to).toScreen(links[i].toGraph));
}

GraphHit GraphPanel::hitAt(ImVec2 mouse) const
{
    const bool hovering = m_hoveredLink >= 0 && m_hoveredLink < int(m_curves.size());
    const HitView view{ scene(), ui().workspace().master().id(), camera(), m_pinned, m_minimap, m_curves, uiScale(),
                        m_hoveredLink, hovering ? insertPoint(m_curves[size_t(m_hoveredLink)], mouse) : ImVec2() };
    return hitTest(view, mouse);
}

void GraphPanel::registerActions(ActionRegistry& actions)
{
    // Interface actions: framing, selecting, opening the add menu. Delete runs node.delete (logged).
    const auto graph = ActionScope::Graph;
    const auto ui    = ActionKind::Interface;
    actions.add({ .id = Actions::GraphDelete, .title = "Delete Selected", .kind = ui,
                  .scope = graph,
                  .enabled = [this] { return !selection().empty() || rowsPicked(); },
                  .run = [this] { if (rowsPicked()) removePickedRows(); else deleteSelection(); } });
    actions.add({ .id = Actions::GraphFrameSelected, .title = "Frame Selected", .kind = ui, .scope = graph,
                  .run = [this] { m_view->autoFrame = false; frameNodes(!selection().empty(), m_surface.size()); } });
    actions.add({ .id = Actions::GraphFrameAll, .title = "Frame All", .kind = ui, .scope = graph,
                  .run = [this] { m_view->autoFrame = false; frameNodes(false, m_surface.size()); } });
    actions.add({ .id = Actions::GraphSelectAll, .title = "Select All", .kind = ui, .scope = graph,
                  .run = [this] {
                      NodeSelection::Set all;
                      for (const auto& [id, s] : scene().items())
                          if (!scene().isMaster(id)) all.insert(id);
                      select(selection().withIds(std::move(all)));
                  } });
    actions.add({ .id = Actions::GraphAddNode, .title = "Add Node...", .kind = ui, .scope = graph,
                  .run = [this] { m_menus.openAdd(GraphMenus::Free{}, camera().toGraph(ImGui::GetIO().MousePos)); } });
    // nodes: the ones to show (none given: the selection), where their output is on the canvas.
    actions.add({ .id = Actions::GraphShowInCanvas, .title = "Show in Canvas", .kind = ui, .args = { Arg::ids(DocArgs::kNodes).optional() },
                  .mouse = "Double-click", .scope = graph,
                  .run = [this](const ActionArgs& a) { showInCanvas(a.has(DocArgs::kNodes) ? a.get(DocArgs::kNodes) : selection().list()); } });
    // node: the one to rename (none given: the one selected), in a field at the mouse.
    actions.add({ .id = Actions::GraphRename, .title = "Rename...", .kind = ui, .args = { Arg::id(DocArgs::kNode).optional() }, .scope = graph,
                  .run = [this](const ActionArgs& a) { return m_menus.openValue(GraphMenus::Rename{ a.get(DocArgs::kNode, selection().single()) }); } });
}

void GraphPanel::showInCanvas(const std::vector<NodeId>& ids)
{
    BoundsI b;
    for (NodeId id : ids)
        if (const Node* n = ui().workspace().find(id)) b.expand(n->bounds());   // a Move: where it moved them to
    ui().actions.run(Actions::CanvasFrame, { { DocArgs::kMinX, b.minX }, { DocArgs::kMinY, b.minY }, { DocArgs::kMaxX, b.maxX }, { DocArgs::kMaxY, b.maxY } });
}

void GraphPanel::deleteSelection()
{
    ui().actions.run(Actions::NodeDelete, { { DocArgs::kNodes, selection().list() } });   // the master is skipped; the rest heal the links around them
}

void GraphPanel::frameNodes(bool selectionOnly, ImVec2 canvasSize)
{
    ImRect r;
    bool any = false;
    for (const auto& [id, s] : scene().items()) {
        if (scene().isMaster(id) || (selectionOnly && !selection().contains(id))) continue;
        if (any) r.Add(s.bounds); else r = s.bounds;
        any = true;
    }
    // Into the part the pinned master leaves free, if that leaves enough.
    ImVec2 area = canvasSize;
    if (!m_pinnedRect.IsInverted()) {
        const float covered = camera().origin().x + canvasSize.x - m_pinnedRect.Min.x + kPinMargin * uiScale();
        if (canvasSize.x - covered > canvasSize.x * 0.4f) area.x -= covered;
    }
    if (any) camera().frame(r.Min, r.Max, area, kFrameMargin);
    else     camera().centerOn(ImVec2(0.0f, 0.0f), area);   // nothing to frame: back to the origin
}

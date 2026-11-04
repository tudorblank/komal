#include "ui/panels/graph/graph-panel.hpp"
#include "base/frames.hpp"
#include "document/document-actions.hpp"
#include "input/input-frame.hpp"
#include "document/workspace.hpp"
#include "ui/panels/graph/internal/layout.hpp"
#include "ui/style/graph-style.hpp"
#include "ui/navigation.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace GraphLayout;
using namespace GraphStyle;
using namespace DocArgs;

namespace {

constexpr Clock::Seconds kRightHold{ 0.25 };   // a right press held this long zooms instead of opening a menu
constexpr float  kRightZoomPerPixel = 1.008f;
constexpr float  kPinScrollStep     = 40.0f;   // points per wheel notch over a tall pinned master

}

void GraphPanel::handleMouse(const GraphHit& hit, bool hovered)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f || ImGui::IsAnyMouseDown())) m_view->autoFrame = false;   // the user's view now

    // ---- navigation
    // Like the canvas: Ctrl+wheel zooms, the wheel scrolls up / down, a sideways wheel left / right.
    if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) && !hit.is<MinimapHit>()) {
        if (ui().input.mods & ModPrimary)        camera().zoomAt(mouse, std::pow(Navigation::kGraphZoomStep, io.MouseWheel));
        else if (scene().isMaster(hit.node())) m_view->pinnedScroll -= io.MouseWheel * kPinScrollStep * uiScale();   // clamped by the layout
        else camera().panBy(ImVec2(io.MouseWheelH * Navigation::kWheelPanStep, io.MouseWheel * Navigation::kWheelPanStep));
    }
    if (m_canvasActive && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) camera().panBy(io.MouseDelta);

    // ---- right button: click opens a menu; hold or drag zooms around the press (right / up: in).
    // Ends whenever the button isn't down, so a release we never saw can't leave it on.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !hit.is<MinimapHit>()) {
        m_rightDown    = true;
        m_rightZooming = false;
        m_rightPress   = mouse;
        m_rightHold    = Deadline::in(kRightHold);
    }
    if (m_rightDown && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (!m_rightZooming && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || m_rightHold.expired()))
            m_rightZooming = true;
        const float d = io.MouseDelta.x - io.MouseDelta.y;
        if (m_rightZooming && d != 0.0f) camera().zoomAt(m_rightPress, std::pow(kRightZoomPerPixel, d));
    } else if (m_rightDown) {
        m_rightDown = false;
        const bool click = !m_rightZooming;
        m_rightZooming = false;
        const NodeItemWidget* rowWidget = hit.as<WidgetHit>() ? widgetOf(scene(), *hit.as<WidgetHit>()) : nullptr;
        const ArtboardControl* board = rowWidget ? rowWidget->as<ArtboardControl>() : nullptr;
        if (click && !dragging() && board) {
            m_menus.openArtboard(board->artboard);
        } else if (click && !dragging()) {
            if (const NodeId id = hit.node(); id != kNoNodeId) {
                if (!selection().contains(id)) select(selection().only(id));
                m_menus.openContext(id);
            } else {
                m_menus.openAdd(GraphMenus::Free{}, camera().toGraph(mouse));
            }
        }
    }

    // ---- left button
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const bool dbl = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        m_press = { hit, mouse, cameraFor(hit.node()).toGraph(mouse), (ui().input.mods & ModPrimary) != 0, (ui().input.mods & ModShift) != 0 };
        if (const auto* port = hit.as<PortHit>()) {
            startLink(*port);
        } else if (const auto* widget = hit.as<WidgetHit>()) {
            pressWidget(*widget, dbl);
        } else if (const auto* node = hit.as<NodeHit>()) {
            pressNode(node->node);
            if (dbl) ui().actions.run(Actions::GraphShowInCanvas, { { kNodes, std::vector<NodeId>{ node->node } } });   // where its pixels are
        } else if (hit.is<MinimapHit>()) {
            m_drag = MinimapDrag{};
            m_minimap.press(mouse, camera(), m_surface.size());
        } else if (const auto* insert = hit.as<InsertHit>()) {
            const GraphLink& l = scene().links()[size_t(insert->link)];
            const ImVec2 at = insertPoint(m_curves[size_t(insert->link)], mouse);
            m_menus.openAdd(GraphMenus::Insert{ l.from, l.to, l.slot }, camera().toGraph(at));   // the node lands right there
        } else if (dbl) {   // a link, or nothing
            m_menus.openAdd(GraphMenus::Free{}, m_press.graph);
        } else {
            m_drag = PressEmpty{ (m_press.ctrl || m_press.shift) ? selection().ids() : NodeSelection::Set{} };
        }
    }
    if (dragging() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) updateDrag();
    if (dragging() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) endDrag(hit);
}

void GraphPanel::endDrag(const GraphHit& hit)
{
    const Drag drag = std::exchange(m_drag, std::monostate{});
    if (const auto* press = std::get_if<PressNode>(&drag)) {   // a click (no drag) on a node of a bigger selection switches to it
        if (press->wasSelected) select(selection().only(m_press.hit.node()));
    } else if (std::holds_alternative<PressEmpty>(drag)) {   // a click on nothing
        if (!m_press.ctrl && !m_press.shift) {
            select({});
            scene().pickRows(kNoNodeId, {}, {});
        }
    } else if (const auto* move = std::get_if<MoveNodes>(&drag)) {
        ui().actions.endGesture(Actions::NodeDrag, { { kDx, move->delta.x }, { kDy, move->delta.y } });
    } else if (const auto* link = std::get_if<LinkDrag>(&drag)) {
        finishLink(*link, hit);
    } else if (const auto* scale = std::get_if<ScaleDrag>(&drag)) {   // the drag is over: now the graph changes, once
        ui().actions.endGesture(Actions::SignalScrub, scale->args);
    } else if (const auto* offset = std::get_if<OffsetDrag>(&drag)) {
        ui().actions.endGesture(Actions::MoveScrub, offset->args);
    } else if (const auto* rows = std::get_if<LayerDrag>(&drag)) {
        if (!rows->lifted) {   // a click: on a row of a bigger pick, just that row
            if (rows->wasPicked) scene().pickRows(rows->node, { rows->pressed }, rows->pressed);
        } else if (const NodeItem* s = scene().item(rows->node); s && rows->at.kind() == StackGap::Kind::Rows && rows->at.target() != StackGap::Target::Home) {   // once, where it lands
            const StackPlace place = landing(*s, rows->at);
            std::vector<NodeId> layers;
            std::vector<ArtboardId> artboards;
            for (const StackRowRef& r : rows->rows) {
                if (r.layer != kNoNodeId) layers.push_back(r.layer);
                else                      artboards.push_back(r.artboard);
            }
            ActionArgs args{ { kNode, rows->node }, { kLayers, layers }, { kLayerTo, place.at } };
            if (!artboards.empty()) args.add({ kArtboards, artboards });
            if (place.into != kNoArtboard) args.add({ kIntoArtboard, place.into });
            if (ui().actions.run(Actions::LayersMove, args)) dropped(rows->node, place);
        }
    }
}

void GraphPanel::pressNode(NodeId id)
{
    const NodeSelection& now = selection();
    if (scene().isMaster(id)) {   // pinned: selected, never moved
        select(now.only(id));
        return;
    }
    scene().bringToFront(id);
    bool wasSelected = false;
    if (m_press.shift) {
        select(now.chained(ui().workspace(), id, m_press.ctrl));
    } else if (m_press.ctrl) {
        select(now.toggled(id));
    } else {
        wasSelected = now.contains(id) && now.size() > 1;
        select(now.contains(id) ? now.withAnchor(id) : now.only(id));
        if (const Node* n = ui().workspace().find(id); n && n->type() == NodeType::Raster && ui().workspace().activeRasterId() != id)
            ui().actions.run(Actions::RasterActivate, { { kNode, id } });   // clicking a raster makes it the one the canvas paints
    }
    const NodeSelection& next = selection();   // the workspace's, as the action left it
    if (!next.contains(id)) return;   // Ctrl+click took it out: nothing to drag
    m_drag = PressNode{ next.list(), wasSelected };
}

void GraphPanel::pressWidget(const WidgetHit& hit, bool doubleClick)
{
    const NodeItem* s = scene().item(hit.node);
    const NodeItemWidget* held = widgetOf(scene(), hit);
    const Node* n = ui().workspace().find(hit.node);
    if (!s || !held || !n) return;
    const NodeItemWidget w = *held;   // the scene may lay the item out again below
    const NodeSettings settings = n->settings();
    scene().bringToFront(hit.node);

    if (w.as<TitleControl>()) {
        if (doubleClick) ui().actions.run(Actions::GraphRename, { { kNode, hit.node } });
        else             pressNode(hit.node);
    } else if (const auto* raster = std::get_if<RasterSettings>(&settings); raster && w.as<LockControl>()) {
        ui().actions.run(Actions::RasterLock, { { kNode, hit.node }, { kOn, !raster->locked } });
    } else if (raster && w.as<LockAlphaControl>()) {
        ui().actions.run(Actions::RasterLockAlpha, { { kNode, hit.node }, { kOn, !raster->alphaLocked } });
    } else if (const auto* section = w.as<SectionControl>()) {
        scene().toggleSection(hit.node, section->signal);
    } else if (const auto* scale = w.as<ScaleControl>()) {
        if (doubleClick) {
            m_menus.openValue(GraphMenus::ScaleValue{ hit.node, scale->signal, scale->channel });
            return;
        }
        for (const NodeItemChannelRow& row : s->channels)   // the track drags; the value text doesn't
            if (row.signal == scale->signal && row.channel == scale->channel && m_press.graph.x <= row.track.Max.x + 5.0f &&
                ui().actions.beginGesture(Actions::SignalScrub, { { kNode, hit.node }, { kSignal, scale->signal }, { kChannel, scale->channel } }))
                m_drag = ScaleDrag{ *scale, {} };
    } else if (const auto* offset = w.as<OffsetControl>()) {
        const auto* move = std::get_if<MoveSettings>(&settings);
        if (!move) return;
        if (doubleClick) {
            m_menus.openValue(GraphMenus::OffsetValue{ hit.node, offset->axis });
            return;
        }
        if (ui().actions.beginGesture(Actions::MoveScrub, { { kNode, hit.node } }))
            m_drag = OffsetDrag{ *offset, offset->axis == 0 ? move->x : move->y, {} };
    } else if (const auto* layer = w.as<LayerControl>()) {
        if (const NodePtr l = ui().workspace().inputAt(hit.node, InputSlot::stackLayer(layer->layer)))
            pressRow(hit.node, { l->id(), kNoArtboard }, m_press.graph.y - w.rect.Min.y);
    } else if (const auto* mute = w.as<LayerMuteControl>()) {
        if (const NodePtr l = ui().workspace().inputAt(hit.node, InputSlot::stackLayer(mute->layer)); n && l)
            ui().actions.run(Actions::LayerMute, { { kNode, hit.node }, { kLayer, l->id() }, { kOn, !stackSettings(*n).mutes(l->id()) } });
    } else if (const auto* board = w.as<ArtboardControl>()) {
        if (doubleClick) m_menus.openValue(GraphMenus::RenameArtboard{ board->artboard });
        else             pressRow(hit.node, { kNoNodeId, board->artboard }, m_press.graph.y - w.rect.Min.y);
    } else if (const auto* fold = w.as<ArtboardFoldControl>()) {
        scene().toggleArtboard(hit.node, fold->artboard);
    } else if (const auto* eye = w.as<ArtboardMuteControl>()) {
        if (const Artboard* a = ui().workspace().artboard(eye->artboard))
            ui().actions.run(Actions::ArtboardSet, { { kArtboard, a->id }, { kVisible, !a->visible } });
    } else if (const auto* color = w.as<ArtboardColorControl>()) {
        m_menus.openColor(color->artboard);
    }
}

Clock::Seconds GraphPanel::nextFrameIn() const
{
    if (!m_surface.shown()) return kNoFrame;
    Clock::Seconds next = kNoFrame;
    if (m_rightDown && !m_rightZooming)   // a right press held long enough turns into a zoom
        next = std::min(next, m_rightHold.left());
    return next;
}

void GraphPanel::updateDrag()
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const NodeId pressed = m_press.hit.node();
    const ImVec2 gm = cameraFor(pressed).toGraph(mouse);
    const bool moved = ImGui::IsMouseDragging(ImGuiMouseButton_Left);

    // A press that moved turns into its drag.
    if (const auto* press = std::get_if<PressNode>(&m_drag)) {
        if (!moved || !ui().actions.beginGesture(Actions::NodeDrag, { { kNodes, press->nodes } })) return;
        m_drag = MoveNodes{};
    } else if (auto* empty = std::get_if<PressEmpty>(&m_drag)) {
        if (!moved) return;
        m_drag = RubberBand{ std::move(empty->base) };
    }

    if (auto* move = std::get_if<MoveNodes>(&m_drag)) {
        move->delta = ImVec2(gm.x - m_press.graph.x, gm.y - m_press.graph.y);
        ui().actions.updateGesture(Actions::NodeDrag, { { kDx, move->delta.x }, { kDy, move->delta.y } });
    } else if (const auto* rubber = std::get_if<RubberBand>(&m_drag)) {
        const ImRect band(ImMin(m_press.graph, gm), ImMax(m_press.graph, gm));
        NodeSelection::Set next = rubber->base;
        for (const auto& [id, s] : scene().items()) {
            if (scene().isMaster(id) || !s.bounds.Overlaps(band)) continue;   // the master isn't in this space
            if (m_press.ctrl && !m_press.shift && rubber->base.contains(id)) next.erase(id);   // Ctrl: toggle
            else next.insert(id);
        }
        select(selection().withIds(std::move(next)));
    } else if (auto* scale = std::get_if<ScaleDrag>(&m_drag)) {
        const NodeItem* s = scene().item(pressed);
        if (!s) return;
        for (const NodeItemChannelRow& row : s->channels)
            if (row.signal == scale->control.signal && row.channel == scale->control.channel) {
                // Previewed only: the graph changes once, on release (see EditPreview).
                const float value = std::round(std::clamp((gm.x - row.track.Min.x) / row.track.GetWidth(), 0.0f, 1.0f) * 1000.0f) / 1000.0f;
                scale->args = { { kNode, pressed }, { kSignal, row.signal }, { kChannel, row.channel }, { kValue, value } };
                ui().actions.updateGesture(Actions::SignalScrub, scale->args);
            }
    } else if (auto* offset = std::get_if<OffsetDrag>(&m_drag)) {
        const Node* n = ui().workspace().find(pressed);
        const NodeSettings settings = n ? n->settings() : NodeSettings{};
        const auto* current = std::get_if<MoveSettings>(&settings);
        if (!current) return;
        const int v = offset->start + int(std::lround(gm.x - m_press.graph.x));   // one pixel per graph unit
        const int x = offset->control.axis == 0 ? v : current->x;
        const int y = offset->control.axis == 1 ? v : current->y;
        offset->args = { { kNode, pressed }, { kOffsetX, x }, { kOffsetY, y } };   // previewed only, applied on release
        ui().actions.updateGesture(Actions::MoveScrub, offset->args);
    } else if (auto* row = std::get_if<LayerDrag>(&m_drag)) {
        const NodeItem* s = scene().item(row->node);
        if (!s) return;
        if (!row->lifted) {
            // Carried from now on. Nothing is dropped anywhere yet: the stack is laid out with its
            // spot kept first (next frame), so the first drop is worked out on that, back home.
            if (moved) row->lifted = true;
            return;
        }
        // One layer pulled out sideways: it comes off the stack, carried as a link.
        constexpr float kPullOut = 40.0f;
        const bool one = row->rows.size() == 1 && row->rows[0].layer != kNoNodeId;
        if (one && (gm.x < s->bounds.Min.x - kPullOut || gm.x > s->bounds.Max.x + kPullOut)) {
            const auto& layers = ui().workspace().find(row->node)->links().layers;
            const auto it = std::find_if(layers.begin(), layers.end(), [&](const NodePtr& l) { return l->id() == row->rows[0].layer; });
            m_drag = std::monostate{};
            if (it == layers.end()) return;
            const InputSlot slot = InputSlot::stackLayer(uint32_t(it - layers.begin()));
            if (ui().actions.beginGesture(Actions::LinkMove, { { kSource, (*it)->id() }, { kFrom, row->node }, { kSlot, slot } }))
                m_drag = LinkDrag{ .node = (*it)->id(), .pickedUp = true };
            return;
        }
        // Where the floating rows' middle is: the gap opens there.
        const bool artboard = std::any_of(row->rows.begin(), row->rows.end(), [](const StackRowRef& r) { return r.artboard != kNoArtboard; });
        row->at = stackPlace(row->node, ImVec2(gm.x, gm.y - row->grab + kLayerRowH * 0.5f), StackGap::Kind::Rows, artboard, row->rows);
    } else if (std::holds_alternative<MinimapDrag>(m_drag)) {   // held on the navigator: the view follows the mouse
        if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f) m_minimap.drag(mouse, camera(), m_surface.size());
    }
}

void GraphPanel::startLink(const PortHit& hit)
{
    const NodeItemPort* port = portOf(scene(), hit);
    if (!port) return;
    const NodeItemPort p = *port;
    if (p.output) {
        m_drag = LinkDrag{ .node = hit.node };
    } else if (const NodePtr src = ui().workspace().inputAt(hit.node, p.slot)) {
        // Lift the link off the input and carry it from its source.
        if (ui().actions.beginGesture(Actions::LinkMove, { { kSource, src->id() }, { kFrom, hit.node }, { kSlot, p.slot } }))
            m_drag = LinkDrag{ .node = src->id(), .pickedUp = true };
    } else {
        m_drag = LinkDrag{ .node = hit.node, .fromOutput = false, .slot = p.slot };
    }
}

StackRowRef GraphPanel::rowRef(NodeId id, const NodeItemLayerRow& row) const
{
    if (const auto* layer = row.as<NodeItemLayerRow::Layer>())
        if (const NodePtr l = ui().workspace().inputAt(id, InputSlot::stackLayer(layer->index))) return { l->id(), kNoArtboard };
    if (const auto* board = row.as<NodeItemLayerRow::Artboard>()) return { kNoNodeId, board->key };
    return {};
}

std::vector<StackRowRef> GraphPanel::shownRows(NodeId id) const
{
    std::vector<StackRowRef> out;
    if (const NodeItem* s = scene().item(id))
        for (const NodeItemLayerRow& row : s->layers)
            if (const StackRowRef r = rowRef(id, row); !r.empty()) out.push_back(r);
    return out;
}

void GraphPanel::pressRow(NodeId id, StackRowRef row, float grab)
{
    // Like a file browser: a click picks the row, Ctrl adds it or takes it out, Shift picks every
    // row from the anchor to it (Ctrl+Shift adds them).
    const NodeItemState* view = scene().viewState(id);
    std::vector<StackRowRef> picked = view ? view->selected : std::vector<StackRowRef>{};
    StackRowRef anchor = view ? view->anchor : StackRowRef{};
    const auto has = [&](const std::vector<StackRowRef>& v, StackRowRef r) { return std::find(v.begin(), v.end(), r) != v.end(); };
    const std::vector<StackRowRef> shown = shownRows(id);
    bool wasPicked = false;
    if (m_press.shift) {
        const auto at = [&](StackRowRef r) { return std::find(shown.begin(), shown.end(), r) - shown.begin(); };
        if (anchor.empty() || !has(shown, anchor)) anchor = row;
        const std::ptrdiff_t from = at(anchor), to = at(row);
        const std::ptrdiff_t lo = std::min(from, to), hi = std::max(from, to);
        if (!m_press.ctrl) picked.clear();
        for (auto i = lo; i <= hi && i < std::ptrdiff_t(shown.size()); ++i)
            if (!has(picked, shown[size_t(i)])) picked.push_back(shown[size_t(i)]);
    } else if (m_press.ctrl) {
        if (has(picked, row)) std::erase(picked, row);
        else picked.push_back(row);
        anchor = row;
    } else {
        wasPicked = has(picked, row) && picked.size() > 1;
        if (!has(picked, row)) picked = { row };
        anchor = row;
    }
    scene().pickRows(id, picked, anchor);
    select({});   // one kind of thing picked at a time: rows, so no nodes
    if (!has(picked, row)) return;   // Ctrl+click took it out: nothing to drag

    // What it carries: the rows picked, top first (an artboard's layers go with it).
    std::vector<StackRowRef> carried;
    for (const StackRowRef& r : shown)
        if (has(picked, r)) carried.push_back(r);
    m_drag = LayerDrag{ .node = id, .rows = std::move(carried), .pressed = row, .grab = grab, .wasPicked = wasPicked, .lifted = false, .at = {} };
}

StackGap GraphPanel::stackPlace(NodeId id, ImVec2 p, StackGap::Kind kind, bool carriesArtboard, const std::vector<StackRowRef>& carried) const
{
    const NodeItem* s = scene().item(id);
    if (!s) return {};
    // On the stack as it shows now, then settled on the stack as it would show it.
    const StackGap drop = stackDrop(*s, p.y, kind, carriesArtboard, carried);
    return settleDrop(drop, [&](const StackGap& g) { return scene().layoutWith(id, { g, carried }); }, p.y, kind, carriesArtboard, carried);
}

void GraphPanel::dropped(NodeId id, const StackPlace& place)
{
    if (place.into == kNoArtboard) return;
    const NodeItemState* view = scene().viewState(id);
    if (view && std::find(view->folded.begin(), view->folded.end(), place.into) != view->folded.end()) scene().toggleArtboard(id, place.into);
}

GraphPanel::LinkTarget GraphPanel::linkTarget(const LinkDrag& link, const GraphHit& hit) const
{
    LinkTarget t;
    const NodeId id = hit.node();
    const NodeItem* item = scene().item(id);
    if (!item) return t;
    const auto* portHit = hit.as<PortHit>();
    const NodeItemPort* port = portHit ? portOf(scene(), *portHit) : nullptr;
    const GraphCamera& camera = cameraFor(id);
    // From an output: a new link, or one lifted off an input (moved by the workspace's own rule).
    const Workspace& workspace = ui().workspace();
    const LinkMoveEdit* lifted = link.pickedUp && workspace.preview() ? workspace.preview()->as<LinkMoveEdit>() : nullptr;
    const auto fits = [&](NodeId to, const InputSlot& slot) {
        return lifted ? workspace.canMoveLink(lifted->source, lifted->from, lifted->slot, to, slot) : workspace.canConnect(link.node, to, slot);
    };
    const ImVec2 at = camera.toGraph(ImGui::GetIO().MousePos);
    const Node* n = workspace.find(id);
    if (!link.fromOutput) {   // looking for an output: its port
        if (port && port->output && workspace.canConnect(id, link.node, link.slot)) t = { true, id, {}, {}, camera.toScreen(port->pos) };
        return t;
    }
    // Over a stack's rows (a little past them): into the stack, at a place, as rows are dropped (it
    // lands on top, then moves there; a lifted one comes off its own row first).
    if (n->ports().inputs == InputShape::Stack && item->stackArea && !(port && port->mask)) {
        ImRect area = *item->stackArea;
        area.Expand(ImVec2(0.0f, kStackBleed));
        if (area.Contains(at)) {
            const bool own = lifted && lifted->from == id;
            const InputSlot slot = InputSlot::newLayer(own ? 0u : uint32_t(n->links().layers.size()));
            if (!fits(id, slot)) return t;
            // A link lifted off this stack's own row: that row is carried (its spot kept).
            const std::vector<StackRowRef> carried = own ? std::vector<StackRowRef>{ { lifted->source, kNoArtboard } } : std::vector<StackRowRef>{};
            t = { true, id, slot, stackPlace(id, at, StackGap::Kind::Link, false, carried), ImGui::GetIO().MousePos };
            if (!t.place.dropping()) return LinkTarget{};   // the stack isn't laid out for it yet
            for (const NodeItemPort& p : item->ports)   // the port at the gap, once it is open
                if (p.placeholder && p.slot.is<InputSlot::NewLayer>()) t.pos = camera.toScreen(p.pos);
            return t;
        }
    }
    if (port && !port->output) {   // on an input's port: that input
        if (fits(id, port->slot)) t = { true, id, port->slot, {}, camera.toScreen(port->pos) };
        return t;
    }
    // On a channel's row (its slider, its value): that channel's mask.
    for (const NodeItemChannelRow& row : item->channels)
        if (row.rect.Contains(at)) {
            const InputSlot slot = InputSlot::mask(row.signal, row.channel);
            if (const NodeItemPort* p = item->input(slot); p && fits(id, slot)) t = { true, id, slot, {}, camera.toScreen(p->pos) };
            return t;
        }
    // On a one-input node's body (its title, its picture): its input. Anywhere else on a node: nothing.
    if (n->ports().inputs == InputShape::One && !item->boxes.empty() && item->boxes.front().rect.Contains(at))
        if (const NodeItemPort* p = item->input(InputSlot::input()); p && fits(id, InputSlot::input()))
            t = { true, id, InputSlot::input(), {}, camera.toScreen(p->pos) };
    return t;
}

void GraphPanel::finishLink(const LinkDrag& link, const GraphHit& hit)
{
    const LinkTarget t = linkTarget(link, hit);
    const bool placed = t.valid && t.place.dropping();
    const NodeItem* stack = placed ? scene().item(t.node) : nullptr;
    const StackPlace place = stack ? landing(*stack, t.place) : StackPlace{};
    const auto placeArgs = [&](ActionArgs args) {   // where in the stack it goes
        args.add({ kPlaceAt, place.at });
        if (place.into != kNoArtboard) args.add({ kIntoArtboard, place.into });
        return args;
    };
    if (link.pickedUp) {   // carried from its source: connects where it lands, else stays off
        const auto* lift = ui().workspace().preview() ? ui().workspace().preview()->as<LinkMoveEdit>() : nullptr;
        if (placed && t.place.target() == StackGap::Target::Home && lift)   // back on its own spot: it stays as it was
            ui().actions.endGesture(Actions::LinkMove, { { kTo, lift->from }, { kToSlot, lift->slot } });
        else if (placed)  ui().actions.endGesture(Actions::LinkMove, placeArgs({ { kTo, t.node }, { kToSlot, t.slot } }));
        else if (t.valid) ui().actions.endGesture(Actions::LinkMove, { { kTo, t.node }, { kToSlot, t.slot } });
        else              ui().actions.endGesture(Actions::LinkMove, { { kDropped, true } });
        if (placed) dropped(t.node, place);
        return;
    }
    if (t.valid) {
        if (placed)               ui().actions.run(Actions::LayerAdd, placeArgs({ { kFrom, link.node }, { kNode, t.node } }));
        else if (link.fromOutput) ui().actions.run(Actions::LinkConnect, { { kFrom, link.node }, { kTo, t.node }, { kSlot, t.slot } });
        else                      ui().actions.run(Actions::LinkConnect, { { kFrom, t.node }, { kTo, link.node }, { kSlot, link.slot } });
        if (placed) dropped(t.node, place);
        return;
    }
    if (!hit.empty() && hit.link() < 0) return;   // on a node, a port or the minimap: no menu

    // Dropped on empty space: pick a node to create there, already connected.
    const ImVec2 at = camera().toGraph(ImGui::GetIO().MousePos);
    if (link.fromOutput) m_menus.openAdd(GraphMenus::FromOutput{ link.node }, at);
    else                 m_menus.openAdd(GraphMenus::ToInput{ link.node, link.slot }, at);
}

int GraphPanel::activeWidget(NodeId id) const
{
    // The widget a scrub holds: the one with the pressed widget's control (its signal and channel).
    std::optional<NodeItemControl> held;
    if (const auto* scale = std::get_if<ScaleDrag>(&m_drag)) held = scale->control;
    if (const auto* offset = std::get_if<OffsetDrag>(&m_drag)) held = offset->control;
    const NodeItem* s = held && id == m_press.hit.node() ? scene().item(id) : nullptr;
    if (!s) return -1;
    for (size_t i = 0; i < s->widgets.size(); ++i)
        if (s->widgets[i].control == *held) return int(i);
    return -1;
}

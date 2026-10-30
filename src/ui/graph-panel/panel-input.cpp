#include "ui/graph-panel/panel.hpp"
#include "base/frames.hpp"
#include "document/document-actions.hpp"
#include "command/input.hpp"
#include "document/project.hpp"
#include "graph/style.hpp"
#include "ui/navigation.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace GraphStyle;

namespace {

constexpr double kRightHoldMs       = 250.0;   // a right press held this long zooms instead of opening a menu
constexpr float  kRightZoomPerPixel = 1.008f;
constexpr float  kPinScrollStep     = 40.0f;   // points per wheel notch over a tall pinned master

}

void GraphPanel::handleMouse(const GraphHit& hit, bool hovered)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f || ImGui::IsAnyMouseDown())) m_autoFrame = false;   // the user's view now

    // ---- navigation
    // Like the canvas: Ctrl+wheel zooms, the wheel scrolls up / down, a sideways wheel left / right.
    if (hovered && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) && !hit.is<MinimapHit>()) {
        if (ui().input.mods & ModPrimary)        m_camera.zoomAt(mouse, std::pow(Navigation::kGraphZoomStep, io.MouseWheel));
        else if (m_scene.isMaster(hit.node())) m_pinnedScroll -= io.MouseWheel * kPinScrollStep * uiScale();   // clamped by the layout
        else m_camera.panBy(ImVec2(io.MouseWheelH * Navigation::kWheelPanStep, io.MouseWheel * Navigation::kWheelPanStep));
    }
    if (m_canvasActive && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_camera.panBy(io.MouseDelta);

    // ---- right button: click opens a menu; hold or drag zooms around the press (right / up: in).
    // Ends whenever the button isn't down, so a release we never saw can't leave it on.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !hit.is<MinimapHit>()) {
        m_rightDown    = true;
        m_rightZooming = false;
        m_rightPress   = mouse;
        m_rightHold    = Deadline::in(kRightHoldMs);
    }
    if (m_rightDown && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (!m_rightZooming && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || m_rightHold.expired()))
            m_rightZooming = true;
        const float d = io.MouseDelta.x - io.MouseDelta.y;
        if (m_rightZooming && d != 0.0f) m_camera.zoomAt(m_rightPress, std::pow(kRightZoomPerPixel, d));
    } else if (m_rightDown) {
        m_rightDown = false;
        const bool click = !m_rightZooming;
        m_rightZooming = false;
        if (click && !dragging()) {
            if (const NodeId id = hit.node(); id != kNoNodeId) {
                if (!selection().contains(id)) select(selection().only(id));
                m_menus.openContext(id);
            } else {
                m_menus.openAdd(GraphMenus::Free{}, m_camera.toGraph(mouse));
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
            if (dbl) ui().actions.run(Actions::GraphShowInCanvas, { { "nodes", std::vector<NodeId>{ node->node } } });   // where its pixels are
        } else if (hit.is<MinimapHit>()) {
            m_drag = MinimapDrag{};
            m_minimap.press(mouse, m_camera, m_surface.size());
        } else if (const auto* insert = hit.as<InsertHit>()) {
            const GraphLink& l = m_scene.links()[size_t(insert->link)];
            const ImVec2 at = insertPoint(m_curves[size_t(insert->link)], mouse);
            m_menus.openAdd(GraphMenus::Insert{ l.from, l.to, l.slot }, m_camera.toGraph(at));   // the node lands right there
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
        if (!m_press.ctrl && !m_press.shift) select({});
    } else if (const auto* move = std::get_if<MoveNodes>(&drag)) {
        ui().actions.endGesture(Actions::NodeDrag, { { "dx", move->delta.x }, { "dy", move->delta.y } });
    } else if (const auto* link = std::get_if<LinkDrag>(&drag)) {
        finishLink(*link, hit);
    } else if (const auto* scale = std::get_if<ScaleDrag>(&drag)) {   // the drag is over: now the graph changes, once
        ui().actions.endGesture(Actions::SignalScrub, scale->args);
    } else if (const auto* offset = std::get_if<OffsetDrag>(&drag)) {
        ui().actions.endGesture(Actions::MoveScrub, offset->args);
    } else if (const auto* layer = std::get_if<LayerDrag>(&drag)) {   // the stack changes once, where the gap is
        if (layer->lifted && layer->at >= 0 && layer->at != int(layer->layer))
            ui().actions.run(Actions::LayerMove, { { "node", layer->node }, { "from", layer->layer }, { "to", layer->at } });
    }
}

void GraphPanel::pressNode(NodeId id)
{
    const NodeSelection& now = selection();
    if (m_scene.isMaster(id)) {   // pinned: selected, never moved
        select(now.only(id));
        return;
    }
    m_scene.bringToFront(id);
    bool wasSelected = false;
    if (m_press.shift) {
        select(now.chained(ui().project, id, m_press.ctrl));
    } else if (m_press.ctrl) {
        select(now.toggled(id));
    } else {
        wasSelected = now.contains(id) && now.size() > 1;
        select(now.contains(id) ? now.withAnchor(id) : now.only(id));
        if (const Node* n = ui().project.find(id); n && n->type() == NodeType::Raster && ui().project.activeRasterId() != id)
            ui().actions.run(Actions::RasterActivate, { { "node", id } });   // clicking a raster makes it the one the canvas paints
    }
    const NodeSelection& next = selection();   // the project's, as the action left it
    if (!next.contains(id)) return;   // Ctrl+click took it out: nothing to drag
    m_drag = PressNode{ next.list(), wasSelected };
}

void GraphPanel::pressWidget(const WidgetHit& hit, bool doubleClick)
{
    const NodeItem* s = m_scene.item(hit.node);
    const NodeItemWidget* held = widgetOf(m_scene, hit);
    const Node* n = ui().project.find(hit.node);
    if (!s || !held || !n) return;
    const NodeItemWidget w = *held;   // the scene may lay the item out again below
    const NodeSettings settings = n->settings();
    m_scene.bringToFront(hit.node);

    if (w.as<TitleControl>()) {
        if (doubleClick) ui().actions.run(Actions::GraphRename, { { "node", hit.node } });
        else             pressNode(hit.node);
    } else if (const auto* raster = std::get_if<RasterSettings>(&settings); raster && w.as<LockControl>()) {
        ui().actions.run(Actions::RasterLock, { { "node", hit.node }, { "on", !raster->locked } });
    } else if (raster && w.as<LockAlphaControl>()) {
        ui().actions.run(Actions::RasterLockAlpha, { { "node", hit.node }, { "on", !raster->alphaLocked } });
    } else if (const auto* section = w.as<SectionControl>()) {
        m_scene.toggleSection(hit.node, section->signal);
    } else if (const auto* scale = w.as<ScaleControl>()) {
        if (doubleClick) {
            m_menus.openValue(GraphMenus::ScaleValue{ hit.node, scale->signal, scale->channel });
            return;
        }
        for (const NodeItemChannelRow& row : s->channels)   // the track drags; the value text doesn't
            if (row.signal == scale->signal && row.channel == scale->channel && m_press.graph.x <= row.track.Max.x + 5.0f &&
                ui().actions.beginGesture(Actions::SignalScrub, { { "node", hit.node }, { "signal", scale->signal }, { "channel", scale->channel } }))
                m_drag = ScaleDrag{ *scale, {} };
    } else if (const auto* offset = w.as<OffsetControl>()) {
        const auto* move = std::get_if<MoveSettings>(&settings);
        if (!move) return;
        if (doubleClick) {
            m_menus.openValue(GraphMenus::OffsetValue{ hit.node, offset->axis });
            return;
        }
        if (ui().actions.beginGesture(Actions::MoveScrub, { { "node", hit.node } }))
            m_drag = OffsetDrag{ *offset, offset->axis == 0 ? move->x : move->y, {} };
    } else if (const auto* layer = w.as<LayerControl>()) {
        m_drag = LayerDrag{ hit.node, layer->layer, m_press.graph.y - w.rect.Min.y };
    }
}

double GraphPanel::nextFrameIn() const
{
    if (!m_surface.shown()) return kNoFrame;
    double next = kNoFrame;
    if (m_rightDown && !m_rightZooming)   // a right press held long enough turns into a zoom
        next = std::min(next, m_rightHold.msLeft() / 1000.0);
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
        if (!moved || !ui().actions.beginGesture(Actions::NodeDrag, { { "nodes", press->nodes } })) return;
        m_drag = MoveNodes{};
    } else if (auto* empty = std::get_if<PressEmpty>(&m_drag)) {
        if (!moved) return;
        m_drag = RubberBand{ std::move(empty->base) };
    }

    if (auto* move = std::get_if<MoveNodes>(&m_drag)) {
        move->delta = ImVec2(gm.x - m_press.graph.x, gm.y - m_press.graph.y);
        ui().actions.updateGesture(Actions::NodeDrag, { { "dx", move->delta.x }, { "dy", move->delta.y } });
    } else if (const auto* rubber = std::get_if<RubberBand>(&m_drag)) {
        const ImRect band(ImMin(m_press.graph, gm), ImMax(m_press.graph, gm));
        NodeSelection::Set next = rubber->base;
        for (const auto& [id, s] : m_scene.items()) {
            if (m_scene.isMaster(id) || !s.bounds.Overlaps(band)) continue;   // the master isn't in this space
            if (m_press.ctrl && !m_press.shift && rubber->base.contains(id)) next.erase(id);   // Ctrl: toggle
            else next.insert(id);
        }
        select(selection().withIds(std::move(next)));
    } else if (auto* scale = std::get_if<ScaleDrag>(&m_drag)) {
        const NodeItem* s = m_scene.item(pressed);
        if (!s) return;
        for (const NodeItemChannelRow& row : s->channels)
            if (row.signal == scale->control.signal && row.channel == scale->control.channel) {
                // Previewed only: the graph changes once, on release (see EditPreview).
                const float value = std::round(std::clamp((gm.x - row.track.Min.x) / row.track.GetWidth(), 0.0f, 1.0f) * 1000.0f) / 1000.0f;
                scale->args = { { "node", pressed }, { "signal", row.signal }, { "channel", row.channel }, { "value", value } };
                ui().actions.updateGesture(Actions::SignalScrub, scale->args);
            }
    } else if (auto* offset = std::get_if<OffsetDrag>(&m_drag)) {
        const Node* n = ui().project.find(pressed);
        const NodeSettings settings = n ? n->settings() : NodeSettings{};
        const auto* current = std::get_if<MoveSettings>(&settings);
        if (!current) return;
        const int v = offset->start + int(std::lround(gm.x - m_press.graph.x));   // one pixel per graph unit
        const int x = offset->control.axis == 0 ? v : current->x;
        const int y = offset->control.axis == 1 ? v : current->y;
        offset->args = { { "node", pressed }, { "x", x }, { "y", y } };   // previewed only, applied on release
        ui().actions.updateGesture(Actions::MoveScrub, offset->args);
    } else if (auto* row = std::get_if<LayerDrag>(&m_drag)) {
        const NodeItem* s = m_scene.item(row->node);
        if (!s) return;
        if (!row->lifted) {
            if (!moved) return;
            row->lifted = true;
        }
        // Pulled out sideways: the layer comes off the stack, carried as a link.
        constexpr float kPullOut = 40.0f;
        if (gm.x < s->bounds.Min.x - kPullOut || gm.x > s->bounds.Max.x + kPullOut) {
            const NodePtr layer = ui().project.inputAt(row->node, InputSlot::stackLayer(row->layer));
            const ActionArgs lifted{ { "source", layer ? layer->id() : kNoNodeId }, { "from", row->node }, { "slot", InputSlot::stackLayer(row->layer).text() } };
            m_drag = std::monostate{};
            if (layer && ui().actions.beginGesture(Actions::LinkMove, lifted)) m_drag = LinkDrag{ .node = layer->id(), .pickedUp = true };
            return;
        }
        // Where the floating row's middle is: the gap opens there.
        row->at = int(stackPosition(row->node, gm.y - row->grab + kLayerRowH * 0.5f));
    } else if (std::holds_alternative<MinimapDrag>(m_drag)) {   // held on the navigator: the view follows the mouse
        if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f) m_minimap.drag(mouse, m_camera, m_surface.size());
    }
}

void GraphPanel::startLink(const PortHit& hit)
{
    const NodeItemPort* port = portOf(m_scene, hit);
    if (!port) return;
    const NodeItemPort p = *port;
    if (p.output) {
        m_drag = LinkDrag{ .node = hit.node };
    } else if (const NodePtr src = ui().project.inputAt(hit.node, p.slot)) {
        // Lift the link off the input and carry it from its source.
        if (ui().actions.beginGesture(Actions::LinkMove, { { "source", src->id() }, { "from", hit.node }, { "slot", p.slot.text() } }))
            m_drag = LinkDrag{ .node = src->id(), .pickedUp = true };
    } else {
        m_drag = LinkDrag{ .node = hit.node, .fromOutput = false, .slot = p.slot };
    }
}

uint32_t GraphPanel::stackPosition(NodeId id, float y) const
{
    // Above every layer row whose middle is below y (so an open gap stays until y passes a middle).
    const NodeItem* s = m_scene.item(id);
    uint32_t at = 0;
    if (s)
        for (const NodeItemLayerRow& row : s->layers)
            if (row.as<NodeItemLayerRow::Layer>() && row.rect.GetCenter().y > y) ++at;
    return at;
}

GraphPanel::LinkTarget GraphPanel::linkTarget(const LinkDrag& link, const GraphHit& hit) const
{
    LinkTarget t;
    const NodeId id = hit.node();
    const NodeItem* item = m_scene.item(id);
    if (!item) return t;
    const auto* portHit = hit.as<PortHit>();
    const NodeItemPort* port = portHit ? portOf(m_scene, *portHit) : nullptr;
    const GraphCamera& camera = cameraFor(id);
    // Anywhere over a compositor (but its mask inputs): inserted into its stack, at the mouse.
    if (link.fromOutput && inputShape(*ui().project.find(id)) == InputShape::Stack && !(port && port->mask)) {
        const InputSlot slot = InputSlot::newLayer(stackPosition(id, camera.toGraph(ImGui::GetIO().MousePos).y));
        if (!ui().project.canConnect(link.node, id, slot)) return t;
        const NodeItemPort* p = item->input(slot);   // the gap's port, once it is open
        return { true, id, slot, p ? camera.toScreen(p->pos) : ImGui::GetIO().MousePos };
    }
    if (port) {
        if (link.fromOutput && !port->output && ui().project.canConnect(link.node, id, port->slot))
            t = { true, id, port->slot, camera.toScreen(port->pos) };
        else if (!link.fromOutput && port->output && ui().project.canConnect(id, link.node, link.slot))
            t = { true, id, {}, camera.toScreen(port->pos) };
    } else if (link.fromOutput) {
        // Dropped on a node's body: its natural input (a unary input, a new top layer).
        InputSlot slot;
        const Node* n = ui().project.find(id);
        if (n && ui().project.preferredInput(id, slot) && ui().project.canConnect(link.node, id, slot))
            if (const NodeItemPort* p = item->input(slot)) t = { true, id, slot, camera.toScreen(p->pos) };
    }
    return t;
}

void GraphPanel::finishLink(const LinkDrag& link, const GraphHit& hit)
{
    const LinkTarget t = linkTarget(link, hit);
    if (link.pickedUp) {   // carried from its source: connects where it lands, else stays off
        if (t.valid) ui().actions.endGesture(Actions::LinkMove, { { "to", t.node }, { "toSlot", t.slot.text() } });
        else         ui().actions.endGesture(Actions::LinkMove, { { "dropped", true } });
        return;
    }
    if (t.valid) {
        if (link.fromOutput) ui().actions.run(Actions::LinkConnect, { { "from", link.node }, { "to", t.node }, { "slot", t.slot.text() } });
        else                 ui().actions.run(Actions::LinkConnect, { { "from", t.node }, { "to", link.node }, { "slot", link.slot.text() } });
        return;
    }
    if (!hit.empty() && hit.link() < 0) return;   // on a node, a port or the minimap: no menu

    // Dropped on empty space: pick a node to create there, already connected.
    const ImVec2 at = m_camera.toGraph(ImGui::GetIO().MousePos);
    if (link.fromOutput) m_menus.openAdd(GraphMenus::FromOutput{ link.node }, at);
    else                 m_menus.openAdd(GraphMenus::ToInput{ link.node, link.slot }, at);
}

int GraphPanel::activeWidget(NodeId id) const
{
    // The widget a scrub holds: the one with the pressed widget's control (its signal and channel).
    std::optional<NodeItemControl> held;
    if (const auto* scale = std::get_if<ScaleDrag>(&m_drag)) held = scale->control;
    if (const auto* offset = std::get_if<OffsetDrag>(&m_drag)) held = offset->control;
    const NodeItem* s = held && id == m_press.hit.node() ? m_scene.item(id) : nullptr;
    if (!s) return -1;
    for (size_t i = 0; i < s->widgets.size(); ++i)
        if (s->widgets[i].control == *held) return int(i);
    return -1;
}

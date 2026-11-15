#include "ui/panels/graph/graph-panel.hpp"
#include "base/frames.hpp"
#include "project/project-actions.hpp"
#include "input/input-frame.hpp"
#include "project/workspace.hpp"
#include "ui/panels/graph/internal/layout.hpp"
#include "ui/navigation.hpp"
#include "base/overloaded.hpp"
#include "ui/panels/graph/internal/link-drop.hpp"
#include "ui/panels/graph/internal/row-pick.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>

using namespace GraphLayout;
using namespace ProjectArgs;

namespace {

constexpr Clock::Seconds kRightHold{ 0.25 };   // a right press held this long zooms instead of opening a menu
constexpr float  kRightZoomPerPixel = 1.008f;
constexpr float  kPinScrollStep     = 40.0f;   // points per wheel notch over a tall pinned master

}

void GraphPanel::handleMouse(const GraphHit& hit, bool hovered)
{
    ImGuiIO& io = ImGui::GetIO();
    const InputFrame& input = ui().input;
    const ImVec2 mouse = io.MousePos;
    if (hovered && (input.wheeled() || ImGui::IsAnyMouseDown())) m_view->autoFrame = false;   // the user's view now

    // ---- navigation
    // Like the canvas: Ctrl+wheel zooms, the wheel scrolls up / down, a sideways wheel left / right.
    if (hovered && input.wheeled() && !hit.is<MinimapHit>()) {
        if (input.mods & ModPrimary)           camera().zoomAt(mouse, std::pow(Navigation::kGraphZoomStep, input.wheelY));
        else if (scene().isMaster(hit.node())) m_view->pinnedScroll -= input.wheelY * kPinScrollStep * uiScale();   // clamped by the layout
        else camera().panBy(ImVec2(input.wheelX * Navigation::kWheelPanStep, input.wheelY * Navigation::kWheelPanStep));
    }
    if (m_buttonActive && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) camera().panBy(io.MouseDelta);

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
        m_press = { hit, mouse, cameraFor(hit.node()).toGraph(mouse), (input.mods & ModPrimary) != 0, (input.mods & ModShift) != 0 };
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
    std::visit(Overloaded{   // every kind of drag ends here: a new one won't compile without its end
        [](std::monostate) {},
        [&](const PressNode& press) {   // a click (no drag) on a node of a bigger selection switches to it
            if (press.wasSelected) select(selection().only(m_press.hit.node()));
        },
        [&](const PressEmpty&) {   // a click on nothing
            if (!m_press.ctrl && !m_press.shift) {
                select({});
                scene().pickRows(kNoNodeId, {}, {});
            }
        },
        [&](const MoveNodes& move) { ui().actions.endGesture(Actions::NodeDrag, { { kDx, move.delta.x }, { kDy, move.delta.y } }); },
        [](const RubberBand&) {},   // its selection is made as it goes
        [&](const LinkDrag& link) { finishLink(link, hit); },
        [&](const ScaleDrag& scale) { ui().actions.endGesture(Actions::SignalScrub, scale.args); },   // the drag is over: now the graph changes, once
        [&](const OffsetDrag& offset) { ui().actions.endGesture(Actions::MoveScrub, offset.args); },
        [&](const LayerDrag& rows) {
            if (!rows.lifted) {   // a click: on a row of a bigger pick, just that row
                if (rows.wasPicked) scene().pickRows(rows.node, { rows.pressed }, rows.pressed);
                return;
            }
            const NodeItem* s = scene().item(rows.node);
            if (!s || rows.at.kind() != StackGap::Kind::Rows || rows.at.target() == StackGap::Target::Home) return;
            const StackPlace place = landing(*s, rows.at);   // once, where it lands
            ActionArgs args = rowArgs(rows.node, rows.rows);
            args.add({ kPlaceAt, place.at });
            if (place.into != kNoArtboard) args.add({ kIntoArtboard, place.into });
            if (ui().actions.run(Actions::LayersMove, args)) dropped(rows.node, place);
        },
        [](const MinimapDrag&) {},
    }, drag);
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

    const auto* raster = std::get_if<RasterSettings>(&settings);
    std::visit(Overloaded{   // every control does something here: a new one won't compile without it
        [&](const TitleControl&) {
            if (doubleClick) ui().actions.run(Actions::GraphRename, { { kNode, hit.node } });
            else             pressNode(hit.node);
        },
        [&](const LockControl&) {
            if (raster) ui().actions.run(Actions::RasterLock, { { kNode, hit.node }, { kOn, !raster->locked } });
        },
        [&](const LockAlphaControl&) {
            if (raster) ui().actions.run(Actions::RasterLockAlpha, { { kNode, hit.node }, { kOn, !raster->alphaLocked } });
        },
        [&](const SectionControl& section) { scene().toggleSection(hit.node, section.signal); },
        [&](const ScaleControl& scale) {   // the track drags
            if (doubleClick) m_menus.openValue(GraphMenus::ScaleValue{ hit.node, scale.signal, scale.channel });
            else if (ui().actions.beginGesture(Actions::SignalScrub, { { kNode, hit.node }, { kSignal, scale.signal }, { kChannel, scale.channel } }))
                m_drag = ScaleDrag{ scale, {} };
        },
        [&](const ScaleValueControl& value) {   // the value types
            if (doubleClick) m_menus.openValue(GraphMenus::ScaleValue{ hit.node, value.signal, value.channel });
        },
        [&](const OffsetControl& offset) {
            const auto* move = std::get_if<MoveSettings>(&settings);
            if (!move) return;
            if (doubleClick) {
                m_menus.openValue(GraphMenus::OffsetValue{ hit.node, offset.axis });
                return;
            }
            if (ui().actions.beginGesture(Actions::MoveScrub, { { kNode, hit.node } }))
                m_drag = OffsetDrag{ offset, offset.axis == 0 ? move->x : move->y, {} };
        },
        [&](const LayerControl& layer) {
            if (const NodePtr l = ui().workspace().inputAt(hit.node, InputSlot::stackLayer(layer.layer)))
                pressRow(hit.node, { l->id(), kNoArtboard }, m_press.graph.y - w.rect.Min.y);
        },
        [&](const LayerMuteControl& mute) {
            if (const NodePtr l = ui().workspace().inputAt(hit.node, InputSlot::stackLayer(mute.layer)))
                ui().actions.run(Actions::LayerMute, { { kNode, hit.node }, { kLayer, l->id() }, { kOn, !stackSettings(*n).mutes(l->id()) } });
        },
        [&](const ArtboardControl& board) {
            if (doubleClick) ui().actions.run(Actions::GraphArtboardRename, { { kArtboard, board.artboard } });
            else             pressRow(hit.node, { kNoNodeId, board.artboard }, m_press.graph.y - w.rect.Min.y);
        },
        [&](const ArtboardFoldControl& fold) { scene().toggleArtboard(hit.node, fold.artboard); },
        [&](const ArtboardMuteControl& eye) {
            if (const Artboard* a = ui().workspace().artboard(eye.artboard))
                ui().actions.run(Actions::ArtboardSet, { { kArtboard, a->id }, { kVisible, !a->visible } });
        },
        [&](const ArtboardColorControl& color) { ui().actions.run(Actions::GraphArtboardColor, { { kArtboard, color.artboard } }); },
    }, w.control);
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

    std::visit(Overloaded{   // every kind of drag follows the mouse here
        [](std::monostate) {},
        [](PressNode&) {},    // not moved yet
        [](PressEmpty&) {},
        [&](MoveNodes& move) {
            move.delta = ImVec2(gm.x - m_press.graph.x, gm.y - m_press.graph.y);
            ui().actions.updateGesture(Actions::NodeDrag, { { kDx, move.delta.x }, { kDy, move.delta.y } });
        },
        [&](const RubberBand& rubber) {
            const ImRect band(ImMin(m_press.graph, gm), ImMax(m_press.graph, gm));
            NodeSelection::Set next = rubber.base;
            for (const auto& [id, s] : scene().items()) {
                if (scene().isMaster(id) || !s.bounds.Overlaps(band)) continue;   // the master isn't in this space
                if (m_press.ctrl && !m_press.shift && rubber.base.contains(id)) next.erase(id);   // Ctrl: toggle
                else next.insert(id);
            }
            select(selection().withIds(std::move(next)));
        },
        [](LinkDrag&) {},   // followed by the panel's frame (updateStackGap, the link target)
        [&](ScaleDrag& scale) {
            const NodeItem* s = scene().item(pressed);
            if (!s) return;
            for (const NodeItemChannelRow& row : s->channels)
                if (row.signal == scale.control.signal && row.channel == scale.control.channel) {
                    // Previewed only: the graph changes once, on release (see EditPreview).
                    const float value = std::round(std::clamp((gm.x - row.track.Min.x) / row.track.GetWidth(), 0.0f, 1.0f) * 1000.0f) / 1000.0f;
                    scale.args = { { kNode, pressed }, { kSignal, row.signal }, { kChannel, row.channel }, { kValue, value } };
                    ui().actions.updateGesture(Actions::SignalScrub, scale.args);
                }
        },
        [&](OffsetDrag& offset) {
            const Node* n = ui().workspace().find(pressed);
            const NodeSettings settings = n ? n->settings() : NodeSettings{};
            const auto* current = std::get_if<MoveSettings>(&settings);
            if (!current) return;
            const int v = offset.start + int(std::lround(gm.x - m_press.graph.x));   // one pixel per graph unit
            const int x = offset.control.axis == 0 ? v : current->x;
            const int y = offset.control.axis == 1 ? v : current->y;
            offset.args = { { kNode, pressed }, { kOffsetX, x }, { kOffsetY, y } };   // previewed only, applied on release
            ui().actions.updateGesture(Actions::MoveScrub, offset.args);
        },
        [&](LayerDrag& row) {
            const NodeItem* s = scene().item(row.node);
            if (!s) return;
            if (!row.lifted) {
                // Carried from now on. Nothing is dropped anywhere yet: the stack is laid out with its
                // spot kept first (next frame), so the first drop is worked out on that, back home.
                if (moved) row.lifted = true;
                return;
            }
            // One layer pulled out sideways: it comes off the stack, carried as a link.
            const bool one = row.rows.size() == 1 && row.rows[0].layer != kNoNodeId;
            if (one && (gm.x < s->bounds.Min.x - kPullOut || gm.x > s->bounds.Max.x + kPullOut)) {
                const NodeId layer = row.rows[0].layer;
                const NodeId stack = row.node;   // read before m_drag changes: `row` is gone after
                const std::optional<size_t> at = Links::layerIndex(*ui().workspace().find(stack), layer);
                m_drag = std::monostate{};
                if (!at) return;
                const InputSlot slot = InputSlot::stackLayer(uint32_t(*at));
                if (ui().actions.beginGesture(Actions::LinkMove, { { kSource, layer }, { kFrom, stack }, { kSlot, slot } }))
                    m_drag = LinkDrag{ .node = layer, .pickedUp = true };
                return;
            }
            // Where the floating rows' middle is: the gap opens there.
            const bool artboard = std::any_of(row.rows.begin(), row.rows.end(), [](const StackRowRef& r) { return r.artboard != kNoArtboard; });
            row.at = stackPlace(row.node, ImVec2(gm.x, gm.y - row.grab + kLayerRowH * 0.5f), StackGap::Kind::Rows, artboard, row.rows);
        },
        [&](MinimapDrag&) {   // held on the navigator: the view follows the mouse
            if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f) m_minimap.drag(mouse, camera(), m_surface.size());
        },
    }, m_drag);
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
    const NodeItemState* view = scene().viewState(id);
    RowPick pick = pickRow(shownRows(id), view ? view->selected : std::vector<StackRowRef>{}, view ? view->anchor : StackRowRef{},
                           row, m_press.ctrl, m_press.shift);
    scene().pickRows(id, pick.picked, pick.anchor);
    select({});   // one kind of thing picked at a time: rows, so no nodes
    if (pick.carried.empty()) return;   // Ctrl+click took it out: nothing to drag
    m_drag = LayerDrag{ .node = id, .rows = std::move(pick.carried), .pressed = row, .grab = grab, .wasPicked = pick.wasPicked, .lifted = false, .at = {} };
}

StackGap GraphPanel::stackPlace(NodeId id, ImVec2 p, StackGap::Kind kind, bool carriesArtboard, const std::vector<StackRowRef>& carried) const
{
    const NodeItem* s = scene().item(id);
    if (!s) return {};
    return ::stackPlace(*s, p.y, kind, carriesArtboard, carried, [&](const StackGap& g) { return scene().layoutWith(id, { g, carried }); });
}

void GraphPanel::dropped(NodeId id, const StackPlace& place)
{
    if (place.into == kNoArtboard) return;
    const NodeItemState* view = scene().viewState(id);
    if (view && std::find(view->folded.begin(), view->folded.end(), place.into) != view->folded.end()) scene().toggleArtboard(id, place.into);
}

GraphPanel::LinkTarget GraphPanel::linkTarget(const LinkDrag& link, const GraphHit& hit) const
{
    const NodeId id = hit.node();
    const NodeItem* item = scene().item(id);
    const Workspace& workspace = ui().workspace();
    const Node* n = workspace.find(id);
    if (!item || !n) return {};
    const auto* portHit = hit.as<PortHit>();
    const GraphCamera& camera = cameraFor(id);
    // From an output: a new link, or one lifted off an input (moved by the workspace's own rule).
    const LinkMoveEdit* lifted = link.pickedUp && workspace.preview() ? workspace.preview()->as<LinkMoveEdit>() : nullptr;
    const LinkEnd end{ link.fromOutput, lifted ? lifted->source : kNoNodeId, lifted ? lifted->from : kNoNodeId };
    const auto fits = [&](const InputSlot& slot) {
        if (!link.fromOutput) return workspace.canConnect(id, link.node, link.slot);
        return lifted ? workspace.canMoveLink(lifted->source, lifted->from, lifted->slot, id, slot) : workspace.canConnect(link.node, id, slot);
    };
    const auto place = [&](ImVec2 at, const std::vector<StackRowRef>& carried) { return stackPlace(id, at, StackGap::Kind::Link, false, carried); };
    const LinkLanding l = linkLanding(*item, *n, camera.toGraph(ImGui::GetIO().MousePos), portHit ? portOf(scene(), *portHit) : nullptr, end, fits, place);
    if (!l.valid) return {};
    return { true, id, l.slot, l.place, camera.toScreen(l.pos) };
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
        else              ui().actions.endGesture(Actions::LinkMove);   // on nothing: it comes off
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

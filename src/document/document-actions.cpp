#include "document/document-actions.hpp"
#include "document/workspace.hpp"

#include <memory>
#include <span>
#include <typeinfo>
#include <utility>
#include <vector>

using namespace DocArgs;

namespace {

// A Move's offset: whole pixels inside the world.
ArgSpec offsetArg(ArgKey<int> key) { return Arg::integer(key, -TileUtils::kWorldLimit, TileUtils::kWorldLimit); }
ArgSpec signalArg()  { return Arg::integer(kSignal, 0, kSignalCount - 1); }
ArgSpec channelArg() { return Arg::integer(kChannel, 0, kChannelCount - 1); }

std::vector<std::string_view> kindIds()
{
    std::vector<std::string_view> ids;
    for (const NodeKind& k : nodeKinds()) ids.push_back(k.id);
    return ids;
}

// What strokeArgs() writes, read back.
BrushParams strokeParams(const ActionArgs& a)
{
    BrushParams b;
    b.diameter     = a.get(kSize);
    b.hardness     = a.get(kHardness);
    b.flow         = a.get(kFlow);
    b.spacing      = a.get(kSpacing);
    b.antialias    = a.get(kAntialias);
    b.color        = a.get(kColor);
    b.opacity      = a.get(kOpacity);
    b.mode         = a.get(kMode) == "erase" ? CompositeMode::Erase : CompositeMode::Over;
    b.pressureSize = a.get(kPressureSize);
    b.pressureFlow = a.get(kPressureFlow);
    return b;
}

}

void registerDocumentActions(ActionRegistry& actions, Workspace& workspace)
{
    using K = ActionKind;
    Workspace& p = workspace;

    // Every document action below is one undo step, named by its title.
    actions.setUndoSteps([&p](const Action& a) { p.beginStep(a.title); }, [&p] { p.endStep(); });

    // ---------------------------------------------------------------- history (moving through it isn't a step)
    // Primary is Cmd on macOS, Ctrl elsewhere.
    actions.add({ .id = Actions::EditUndo, .title = "Undo", .kind = K::DocumentEdit,
                  .repeats = true, .undoStep = false, .enabled = [&p] { return p.canUndo(); }, .run = [&p] { return p.undo(); } });
    actions.add({ .id = Actions::EditRedo, .title = "Redo", .kind = K::DocumentEdit,
                  .repeats = true, .undoStep = false, .enabled = [&p] { return p.canRedo(); }, .run = [&p] { return p.redo(); } });
    // step: any step of the history tree, across branches (the History Tree panel).
    actions.add({ .id = Actions::HistoryGoTo, .title = "Go to History Step", .kind = K::DocumentEdit, .args = { Arg::integer(kStep, 0) },
                  .undoStep = false, .enabled = [&p] { return !p.strokeActive() && p.history().size() > 1; },
                  .run = [&p](const ActionArgs& a) { return p.goToHistory(a.get(kStep)); } });

    // ---------------------------------------------------------------- nodes
    // Optionally fed from `from` (into its natural input), or feeding `into` at `slot`.
    actions.add({ .id = Actions::NodeAdd, .title = "Add Node", .kind = K::DocumentEdit,
                  .args = { Arg::choice(kType, kindIds()), Arg::number(kX), Arg::number(kY), Arg::id(kFrom).optional(),
                            Arg::id(kInto).optional(), Arg::parsed(kSlot).optional() },
                  .run = [&p](const ActionArgs& a) -> ActionResult {
        Workspace::NodeWiring wiring;
        wiring.from = a.get(kFrom, kNoNodeId);
        if (a.has(kInto)) {
            wiring.into = a.get(kInto);
            wiring.slot = a.get(kSlot, InputSlot::input());
        }
        const NodeId id = p.addNode(findNodeKind(a.get(kType))->type, a.get(kX), a.get(kY), wiring);
        return { id != kNoNodeId, int64_t(id) };
    } });
    actions.add({ .id = Actions::NodeReference, .title = "Add Reference", .kind = K::DocumentEdit,
                  .args = { Arg::id(kNode), Arg::number(kX), Arg::number(kY) }, .run = [&p](const ActionArgs& a) -> ActionResult {
        // Not of the master: it's a sink.
        const NodeId id = p.addNode(NodeType::Reference, a.get(kX), a.get(kY), { .from = a.get(kNode), .into = kNoNodeId, .slot = {} });
        return { id != kNoNodeId, int64_t(id) };
    } });
    actions.add({ .id = Actions::NodeDelete, .title = "Delete Nodes", .kind = K::DocumentEdit, .args = { Arg::ids(kNodes) },
                  .run = [&p](const ActionArgs& a) { return p.deleteNodes(a.get(kNodes)); } });
    actions.add({ .id = Actions::NodeRename, .title = "Rename", .kind = K::DocumentEdit, .args = { Arg::id(kNode), Arg::text(kLabel) },
                  .run = [&p](const ActionArgs& a) { return p.setNodeLabel(a.get(kNode), a.get(kLabel)); } });

    // ---------------------------------------------------------------- rasters
    // Picking the raster to paint into changes nothing undo restores.
    actions.add({ .id = Actions::RasterActivate, .title = "Paint into This Raster", .kind = K::DocumentEdit, .args = { Arg::id(kNode) },
                  .undoStep = false, .run = [&p](const ActionArgs& a) { return p.setActiveRaster(a.get(kNode)); } });
    // Its RasterSettings, one of them changed.
    const auto rasterSetting = [&p](NodeId id, bool RasterSettings::* field, bool on) {
        const Node* n = p.find(id);
        const NodeSettings current = n ? n->settings() : NodeSettings{};
        const auto* now = std::get_if<RasterSettings>(&current);
        if (!now) return false;
        RasterSettings s = *now;
        s.*field = on;
        return p.setNodeSettings(id, s);
    };
    actions.add({ .id = Actions::RasterLock, .title = "Lock Pixels", .kind = K::DocumentEdit, .args = { Arg::id(kNode), Arg::flag(kOn) },
                  .run = [rasterSetting](const ActionArgs& a) { return rasterSetting(a.get(kNode), &RasterSettings::locked, a.get(kOn)); } });
    actions.add({ .id = Actions::RasterLockAlpha, .title = "Lock Transparency", .kind = K::DocumentEdit, .args = { Arg::id(kNode), Arg::flag(kOn) },
                  .run = [rasterSetting](const ActionArgs& a) { return rasterSetting(a.get(kNode), &RasterSettings::alphaLocked, a.get(kOn)); } });

    // ---------------------------------------------------------------- links
    actions.add({ .id = Actions::LinkConnect, .title = "Connect", .kind = K::DocumentEdit, .args = { Arg::id(kFrom), Arg::id(kTo), Arg::parsed(kSlot) },
                  .run = [&p](const ActionArgs& a) { return p.connect(a.get(kFrom), a.get(kTo), a.get(kSlot)); } });
    actions.add({ .id = Actions::LinkDisconnect, .title = "Disconnect", .kind = K::DocumentEdit, .args = { Arg::id(kTo), Arg::parsed(kSlot) },
                  .run = [&p](const ActionArgs& a) { return p.disconnect(a.get(kTo), a.get(kSlot)); } });
    // Rows of a stack moved together (layers, and the master's artboards whole) to one place: a
    // position among the rows left, at the top level or into an artboard (Workspace::moveStackRows).
    actions.add({ .id = Actions::LayersMove, .title = "Move Layers", .kind = K::DocumentEdit,
                  .args = { Arg::id(kNode), Arg::ids(kLayers), Arg::ids(kArtboards).optional(), Arg::id(kIntoArtboard).optional(),
                            Arg::integer(kLayerTo, 0) }, .run = [&p](const ActionArgs& a) {
        StackMove m;
        m.layers = a.get(kLayers);
        m.groups = a.get(kArtboards, {});
        m.into   = a.get(kIntoArtboard, kNoArtboard);
        m.at   = a.get(kLayerTo);
        return p.moveStackRows(a.get(kNode), m);
    } });
    // A link dropped on a stack: `from` linked into it at a place (layers.move's), into an artboard or not.
    actions.add({ .id = Actions::LayerAdd, .title = "Connect", .kind = K::DocumentEdit,
                  .args = { Arg::id(kFrom), Arg::id(kNode), Arg::id(kIntoArtboard).optional(), Arg::integer(kPlaceAt, 0) },
                  .run = [&p](const ActionArgs& a) {
        return p.addLayer(a.get(kFrom), a.get(kNode), a.get(kIntoArtboard, kNoArtboard), a.get(kPlaceAt));
    } });
    // Del on rows picked in a stack: its layers unlinked, the master's artboards deleted.
    actions.add({ .id = Actions::LayersRemove, .title = "Remove from Stack", .kind = K::DocumentEdit,
                  .args = { Arg::id(kNode), Arg::ids(kLayers), Arg::ids(kArtboards).optional() }, .run = [&p](const ActionArgs& a) {
        return p.removeStackRows(a.get(kNode), a.get(kLayers), a.get(kArtboards, {}));
    } });
    actions.add({ .id = Actions::LayerMute, .title = "Mute Layer", .kind = K::DocumentEdit,
                  .args = { Arg::id(kNode), Arg::id(kLayer), Arg::flag(kOn) }, .run = [&p](const ActionArgs& a) {
        return p.setLayerMuted(a.get(kNode), a.get(kLayer), a.get(kOn));
    } });

    // ---------------------------------------------------------------- artboards (the master's)
    // A new one over the rect (world pixels, inclusive): empty, transparent, on top of the stack.
    actions.add({ .id = Actions::ArtboardAdd, .title = "Add Artboard", .kind = K::DocumentEdit,
                  .args = { Arg::integer(kMinX), Arg::integer(kMinY), Arg::integer(kMaxX), Arg::integer(kMaxY) },
                  .run = [&p](const ActionArgs& a) -> ActionResult {
        const ArtboardId id = p.addArtboard({ a.get(kMinX), a.get(kMinY), a.get(kMaxX), a.get(kMaxY) });
        return { id != kNoArtboard, int64_t(id) };
    } });
    // What's given changes; the rest stays.
    actions.add({ .id = Actions::ArtboardSet, .title = "Change Artboard", .kind = K::DocumentEdit,
                  .args = { Arg::id(kArtboard), Arg::text(kName).optional(), Arg::parsed(kColor).optional(), Arg::flag(kVisible).optional() },
                  .run = [&p](const ActionArgs& a) {
        const Artboard* now = p.artboard(a.get(kArtboard));
        if (!now) return false;
        Artboard next = *now;
        next.name    = a.get(kName, next.name);
        next.color   = a.get(kColor, next.color);
        next.visible = a.get(kVisible, next.visible);
        return p.setArtboard(next);
    } });
    actions.add({ .id = Actions::ArtboardDelete, .title = "Delete Artboard", .kind = K::DocumentEdit, .args = { Arg::id(kArtboard) },
                  .run = [&p](const ActionArgs& a) { return p.deleteArtboard(a.get(kArtboard)); } });

    // ---------------------------------------------------------------- values
    actions.add({ .id = Actions::SignalSet, .title = "Set Signal", .kind = K::DocumentEdit,
                  .args = { Arg::id(kNode), signalArg(), channelArg(), Arg::number(kValue, 0.0, 1.0) }, .run = [&p](const ActionArgs& a) {
        return p.setSignalScale(a.get(kNode), a.get(kSignal), a.get(kChannel), a.get(kValue));
    } });
    actions.add({ .id = Actions::MoveSet, .title = "Set Offset", .kind = K::DocumentEdit,
                  .args = { Arg::id(kNode), offsetArg(kOffsetX), offsetArg(kOffsetY) }, .run = [&p](const ActionArgs& a) {
        return p.setNodeSettings(a.get(kNode), MoveSettings{ a.get(kOffsetX), a.get(kOffsetY) });
    } });

    // ---------------------------------------------------------------- import
    // target: where it was dropped (x y: a canvas point, or the node's place in the graph).
    // Payload: the tiles, already where they go.
    actions.add({ .id = Actions::ImageImport, .title = "Import Image", .kind = K::DocumentEdit,
                  .args = { Arg::text(kPath), Arg::text(kName), Arg::choice(kTarget, { "default", "canvas", "graph" }),
                            Arg::number(kX), Arg::number(kY) },
                  .payload = &typeid(const TileGrid*),
                  .run = [&p](const ActionArgs& a) -> ActionResult {
        const TileGrid* const* tiles = a.payloadAs<const TileGrid*>();
        if (!tiles || !*tiles) return {};
        Workspace::ImportOptions options;
        if (a.get(kTarget) == "graph") {   // a node where it was dropped, linked to nothing
            options.addToMaster = false;
            options.makeActive  = false;
            options.placeNode   = true;
            options.nodeX = a.get(kX);
            options.nodeY = a.get(kY);
        }
        const NodeId id = p.importTiles(**tiles, a.get(kName), options);
        return { id != kNoNodeId, int64_t(id) };
    } });

    // ---------------------------------------------------------------- what the views share
    // Not the document: never undone, never logged. The canvas and the graph follow these.
    const auto ui = K::Interface;
    // nodes: the new selection (none: nothing); anchor: what Shift+click chains from (0: none).
    actions.add({ .id = Actions::SelectionSet, .title = "Select", .kind = ui, .args = { Arg::ids(kNodes), Arg::integer(kAnchor, 0) },   // selectionArgs()
                  .run = [&p](const ActionArgs& a) {
        NodeSelection::Set ids;
        for (const NodeId id : a.get(kNodes)) ids.insert(id);
        return p.setSelection({ std::move(ids), a.get(kAnchor) });
    } });

    // The canvas camera, while the canvas is shown. Points (x y) are in the canvas viewport (none:
    // its middle); world points are canvas pixels.
    const auto shown = [&p] { return p.canvasViewport().x > 0.0f && p.canvasViewport().y > 0.0f; };
    const auto camera = [&p](auto change) {
        CanvasCamera c = p.canvasCamera();
        change(c, p.canvasViewport());
        return p.setCanvasCamera(c);
    };
    const auto pointArg = [](const ActionArgs& a, Vec2 viewport) {
        return a.has(kX) ? Vec2{ a.get(kX), a.get(kY) } : Vec2{ viewport.x * 0.5f, viewport.y * 0.5f };
    };
    actions.add({ .id = Actions::CanvasViewport, .title = "Canvas Size", .kind = ui,
                  .args = { Arg::number(kWidth, 0), Arg::number(kHeight, 0) },
                  .run = [&p](const ActionArgs& a) { return p.setCanvasViewport({ a.get(kWidth), a.get(kHeight) }); } });
    actions.add({ .id = Actions::CanvasPan, .title = "Pan", .kind = ui, .args = { Arg::number(kDx), Arg::number(kDy) },
                  .enabled = shown, .run = [camera](const ActionArgs& a) {
        return camera([&](CanvasCamera& c, Vec2) { c.panBy({ a.get(kDx), a.get(kDy) }); });
    } });
    actions.add({ .id = Actions::CanvasZoom, .title = "Zoom", .kind = ui,
                  .args = { Arg::number(kFactor, 1e-6), Arg::number(kX).optional(), Arg::number(kY).optional() },
                  .enabled = shown, .run = [camera, pointArg](const ActionArgs& a) {
        return camera([&](CanvasCamera& c, Vec2 viewport) { c.zoomAt(pointArg(a, viewport), a.get(kFactor)); });
    } });
    actions.add({ .id = Actions::CanvasRotate, .title = "Rotate", .kind = ui,
                  .args = { Arg::number(kDegrees), Arg::number(kX).optional(), Arg::number(kY).optional() },
                  .enabled = shown, .run = [camera, pointArg](const ActionArgs& a) {
        return camera([&](CanvasCamera& c, Vec2 viewport) { c.rotateAt(pointArg(a, viewport), toRadians(a.get(kDegrees))); });
    } });
    actions.add({ .id = Actions::CanvasCenterOn, .title = "Center On", .kind = ui, .args = { Arg::number(kX), Arg::number(kY) },
                  .enabled = shown, .run = [camera](const ActionArgs& a) {
        return camera([&](CanvasCamera& c, Vec2 viewport) { c.centerOn(a.get(kX), a.get(kY), viewport.x, viewport.y); });
    } });
    // A world rectangle, fitted (maxX < minX: none, the origin centred).
    actions.add({ .id = Actions::CanvasFrame, .title = "Frame", .kind = ui,
                  .args = { Arg::integer(kMinX), Arg::integer(kMinY), Arg::integer(kMaxX), Arg::integer(kMaxY) },
                  .enabled = shown, .run = [camera](const ActionArgs& a) {
        const BoundsI b{ a.get(kMinX), a.get(kMinY), a.get(kMaxX), a.get(kMaxY) };
        return camera([&](CanvasCamera& c, Vec2 viewport) {
            if (b.empty()) c.centerOn(0.0f, 0.0f, viewport.x, viewport.y);
            else           c.frame(float(b.minX), float(b.minY), float(b.maxX + 1), float(b.maxY + 1), viewport.x, viewport.y);
        });
    } });
    actions.add({ .id = Actions::CanvasFrameAll, .title = "Frame All", .kind = ui, .scope = ActionScope::Canvas,
                  .enabled = shown, .run = [&actions, &p] {   // lost? back to the picture
        const BoundsI b = p.master().bounds();
        return actions.run(Actions::CanvasFrame, { { kMinX, b.minX }, { kMinY, b.minY }, { kMaxX, b.maxX }, { kMaxY, b.maxY } });
    } });

    // ---------------------------------------------------------------- gestures
    // A stroke into the active raster: one undo step. Begun from its arguments alone (strokeArgs():
    // the brush and the first point, all logged); the rest of its points stream in as they're
    // drawn (payloads: spans of StrokePoint).
    actions.add({ .id = Actions::BrushStroke, .title = "Brush Stroke", .kind = K::Gesture,
                  .args = { Arg::id(kRaster), Arg::number(kX), Arg::number(kY), Arg::number(kPressure, 0.0, 1.0),
                            Arg::choice(kDevice, { "mouse", "pen" }), Arg::choice(kMode, { "paint", "erase" }),
                            Arg::number(kSize, BrushParams::kMinDiameter, BrushParams::kMaxDiameter), Arg::number(kHardness, 0.0, 1.0),
                            Arg::number(kFlow, BrushParams::kMinFlow, 1.0), Arg::number(kSpacing, BrushParams::kMinSpacing, 1.0),
                            Arg::integer(kOpacity, 0, 255), Arg::parsed(kColor),
                            Arg::flag(kAntialias), Arg::flag(kPressureSize), Arg::flag(kPressureFlow) },
                  .endArgs = { Arg::integer(kPoints, 0) },
                  .updatePayload = &typeid(std::span<const StrokePoint>),
                  .begin = [&p](const ActionArgs& a) {
                      // Into the raster it names, which must be the one being painted.
                      if (a.get(kRaster) != p.activeRasterId() || !p.activeRasterEditable()) return false;
                      p.beginStroke(strokeParams(a), { a.get(kX), a.get(kY), a.get(kPressure) });
                      return true;
                  },
                  .update = [&p](const ActionArgs& a) {
                      if (const auto* path = a.payloadAs<std::span<const StrokePoint>>()) p.strokeTo(*path);
                  },
                  .end    = [&p] { p.endStroke(); },
                  .cancel = [&p] { p.cancelStroke(); } });

    // Nodes dragged in the graph: dx dy from where they were. Previewed while dragging (the graph
    // shows them there), moved once at the end.
    auto dragged = std::make_shared<std::vector<NodeId>>();
    const auto dragTo = [&p, dragged](const ActionArgs& a) {
        return p.previewEdit(NodesMoveEdit{ *dragged, a.get(kDx), a.get(kDy) });
    };
    actions.add({ .id = Actions::NodeDrag, .title = "Move Nodes", .kind = K::Gesture,
                  .args = { Arg::ids(kNodes) }, .endArgs = { Arg::number(kDx), Arg::number(kDy) },
                  .updateArgs = { Arg::number(kDx), Arg::number(kDy) },
                  .begin = [&p, dragged](const ActionArgs& a) {
                      dragged->clear();
                      for (const NodeId id : a.get(kNodes))
                          if (p.find(id)) dragged->push_back(id);
                      return !dragged->empty();
                  },
                  .update = dragTo,
                  .end    = [&p, dragTo](const ActionArgs& a) { if (dragTo(a)) p.applyPreview(); },
                  .cancel = [&p] { p.cancelPreview(); } });

    // A value dragged on a node: previewed while dragging, applied once at the end.
    const auto previewScale = [&p](const ActionArgs& a) {
        return p.previewEdit(ScaleEdit{ a.get(kNode), a.get(kSignal), a.get(kChannel), a.get(kValue) });
    };
    const auto previewOffset = [&p](const ActionArgs& a) {
        return p.previewEdit(OffsetEdit{ a.get(kNode), a.get(kOffsetX), a.get(kOffsetY) });
    };
    actions.add({ .id = Actions::SignalScrub, .title = "Change Signal", .kind = K::Gesture,
                  .args = { Arg::id(kNode), signalArg(), channelArg() },
                  .endArgs = { Arg::id(kNode), signalArg(), channelArg(), Arg::number(kValue, 0.0, 1.0) },
                  .updateArgs = { Arg::id(kNode), signalArg(), channelArg(), Arg::number(kValue, 0.0, 1.0) },
                  .begin  = [&p](const ActionArgs& a) { return p.find(a.get(kNode)) != nullptr; },
                  .update = previewScale,
                  .end    = [&p, previewScale](const ActionArgs& a) { if (previewScale(a)) p.applyPreview(); },
                  .cancel = [&p] { p.cancelPreview(); } });
    actions.add({ .id = Actions::MoveScrub, .title = "Change Offset", .kind = K::Gesture,
                  .args = { Arg::id(kNode) }, .endArgs = { Arg::id(kNode), offsetArg(kOffsetX), offsetArg(kOffsetY) },
                  .updateArgs = { Arg::id(kNode), offsetArg(kOffsetX), offsetArg(kOffsetY) },
                  .begin  = [&p](const ActionArgs& a) { return p.find(a.get(kNode)) != nullptr; },
                  .update = previewOffset,
                  .end    = [&p, previewOffset](const ActionArgs& a) { if (previewOffset(a)) p.applyPreview(); },
                  .cancel = [&p] { p.cancelPreview(); } });

    // A link lifted off an input (source from slot) and carried, previewed: dropped on an input (to
    // toSlot) it moves there, dropped on nothing it comes off; into a stack, `at` (and `into`) put it
    // at a place there. Cancelled, nothing changes.
    actions.add({ .id = Actions::LinkMove, .title = "Move Link", .kind = K::Gesture,
                  .args = { Arg::id(kSource), Arg::id(kFrom), Arg::parsed(kSlot) },
                  .endArgs = { Arg::id(kTo).optional(), Arg::parsed(kToSlot).optional(), Arg::flag(kDropped).optional(),
                               Arg::id(kIntoArtboard).optional(), Arg::integer(kPlaceAt, 0).optional() },
                  .begin = [&p](const ActionArgs& a) { return p.previewEdit(LinkMoveEdit{ a.get(kSource), a.get(kFrom), a.get(kSlot) }); },
                  .end   = [&p](const ActionArgs& a) {
                      const auto* lifted = p.preview() ? p.preview()->as<LinkMoveEdit>() : nullptr;
                      if (!lifted) return false;
                      const LinkMoveEdit link = *lifted;
                      p.cancelPreview();
                      const NodeId to = a.get(kTo, kNoNodeId);
                      if (!p.moveLink(link.source, link.from, link.slot, to, a.get(kToSlot, InputSlot{}))) return false;
                      // Into a stack at a place (layers.move's): then moved there.
                      if (a.has(kPlaceAt)) p.moveStackRows(to, { { link.source }, {}, a.get(kIntoArtboard, kNoArtboard), a.get(kPlaceAt) });
                      return true;
                  },
                  .cancel = [&p] { p.cancelPreview(); } });
}

ActionArgs selectionArgs(const NodeSelection& selection)
{
    return { { kNodes, selection.list() }, { kAnchor, selection.anchor() } };
}

ActionArgs strokeArgs(NodeId raster, const BrushParams& b, StrokePoint at, bool pen)
{
    return { { kRaster, raster }, { kX, at.x }, { kY, at.y }, { kPressure, at.pressure }, { kDevice, pen ? "pen" : "mouse" },
             { kMode, b.mode == CompositeMode::Erase ? "erase" : "paint" }, { kSize, b.diameter }, { kHardness, b.hardness },
             { kFlow, b.flow }, { kSpacing, b.spacing }, { kOpacity, b.opacity }, { kColor, b.color }, { kAntialias, b.antialias },
             { kPressureSize, b.pressureSize }, { kPressureFlow, b.pressureFlow } };
}

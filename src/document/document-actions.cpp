#include "document/document-actions.hpp"
#include "document/project.hpp"

#include <SDL3/SDL_keycode.h>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace {

// Read only after the registry checked them against the action's spec.
NodeId nodeArg(const ActionArgs& a, std::string_view name) { return NodeId(a.integer(name)); }
InputSlot slotArg(const ActionArgs& a, std::string_view name = "slot") { return *InputSlot::parse(a.text(name)); }

ArgSpec slotSpec(std::string_view name = "slot") { return Arg::text(name).checked(&InputSlot::valid); }
// A Move's offset: whole pixels inside the world.
ArgSpec offsetArg(std::string_view name) { return Arg::integer(name, -TileUtils::kWorldLimit, TileUtils::kWorldLimit); }

ArgSpec signalArg()  { return Arg::integer("signal", 0, kSignalCount - 1); }
ArgSpec channelArg() { return Arg::integer("channel", 0, kChannelCount - 1); }

std::vector<std::string_view> kindIds()
{
    std::vector<std::string_view> ids;
    for (const NodeKind& k : nodeKinds()) ids.push_back(k.id);
    return ids;
}

}

void registerDocumentActions(ActionRegistry& actions, Project& project)
{
    using K = ActionKind;
    Project& p = project;

    // Every document action below is one undo step, named by its title.
    actions.setUndoSteps([&p](const Action& a) { p.beginStep(a.title); }, [&p] { p.endStep(); });

    // ---------------------------------------------------------------- history (moving through it isn't a step)
    // Primary is Cmd on macOS, Ctrl elsewhere.
    actions.add({ .id = Actions::EditUndo, .title = "Undo", .kind = K::DocumentEdit, .shortcuts = { { SDLK_Z, ModPrimary } },
                  .repeats = true, .undoStep = false, .enabled = [&p] { return p.canUndo(); }, .run = [&p] { return p.undo(); } });
    actions.add({ .id = Actions::EditRedo, .title = "Redo", .kind = K::DocumentEdit,
                  .shortcuts = { { SDLK_Z, ModPrimary | ModShift }, { SDLK_Y, ModPrimary } },
                  .repeats = true, .undoStep = false, .enabled = [&p] { return p.canRedo(); }, .run = [&p] { return p.redo(); } });
    // step: any step of the history tree, across branches (the History Tree panel).
    actions.add({ .id = Actions::HistoryGoTo, .title = "Go to History Step", .kind = K::DocumentEdit, .args = { Arg::integer("step", 0) },
                  .undoStep = false, .enabled = [&p] { return !p.strokeActive() && p.history().size() > 1; },
                  .run = [&p](const ActionArgs& a) { return p.goToHistory(StepId(a.integer("step"))); } });

    // ---------------------------------------------------------------- nodes
    // Optionally fed from `from` (into its natural input), or feeding `into` at `slot`.
    actions.add({ .id = Actions::NodeAdd, .title = "Add Node", .kind = K::DocumentEdit,
                  .args = { Arg::choice("type", kindIds()), Arg::number("x"), Arg::number("y"), Arg::id("from").optional(),
                            Arg::id("into").optional(), slotSpec().optional() },
                  .run = [&p](const ActionArgs& a) -> ActionResult {
        const NodeId id = p.createNode(findNodeKind(a.text("type"))->type, float(a.number("x")), float(a.number("y")));
        InputSlot in;
        const bool hasInput = p.preferredInput(id, in);
        bool fed = !a.has("from");
        if (!fed && hasInput) fed = p.connect(nodeArg(a, "from"), id, in);
        if (a.has("into") && fed) p.connect(id, nodeArg(a, "into"), a.has("slot") ? slotArg(a) : InputSlot::input());
        return { true, int64_t(id) };
    } });
    actions.add({ .id = Actions::NodeReference, .title = "Add Reference", .kind = K::DocumentEdit,
                  .args = { Arg::id("node"), Arg::number("x"), Arg::number("y") }, .run = [&p](const ActionArgs& a) -> ActionResult {
        const NodeId id = p.createNode(NodeType::Reference, float(a.number("x")), float(a.number("y")));
        p.connect(nodeArg(a, "node"), id, InputSlot::input());   // not from the master: it's a sink
        return { true, int64_t(id) };
    } });
    actions.add({ .id = Actions::NodeDelete, .title = "Delete Nodes", .kind = K::DocumentEdit, .args = { Arg::ids("nodes") },
                  .run = [&p](const ActionArgs& a) {
        std::vector<NodeId> ids;
        for (int64_t i : a.list("nodes")) ids.push_back(NodeId(i));
        return p.deleteNodes(ids);
    } });
    actions.add({ .id = Actions::NodeRename, .title = "Rename", .kind = K::DocumentEdit, .args = { Arg::id("node"), Arg::text("label") },
                  .run = [&p](const ActionArgs& a) { return p.setNodeLabel(nodeArg(a, "node"), a.text("label")); } });

    // ---------------------------------------------------------------- rasters
    // Picking the raster to paint into changes nothing undo restores.
    actions.add({ .id = Actions::RasterActivate, .title = "Paint into This Raster", .kind = K::DocumentEdit, .args = { Arg::id("node") },
                  .undoStep = false, .run = [&p](const ActionArgs& a) { return p.setActiveRaster(nodeArg(a, "node")); } });
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
    actions.add({ .id = Actions::RasterLock, .title = "Lock Pixels", .kind = K::DocumentEdit, .args = { Arg::id("node"), Arg::flag("on") },
                  .run = [rasterSetting](const ActionArgs& a) { return rasterSetting(nodeArg(a, "node"), &RasterSettings::locked, a.flag("on")); } });
    actions.add({ .id = Actions::RasterLockAlpha, .title = "Lock Transparency", .kind = K::DocumentEdit, .args = { Arg::id("node"), Arg::flag("on") },
                  .run = [rasterSetting](const ActionArgs& a) { return rasterSetting(nodeArg(a, "node"), &RasterSettings::alphaLocked, a.flag("on")); } });

    // ---------------------------------------------------------------- links
    actions.add({ .id = Actions::LinkConnect, .title = "Connect", .kind = K::DocumentEdit, .args = { Arg::id("from"), Arg::id("to"), slotSpec() },
                  .run = [&p](const ActionArgs& a) { return p.connect(nodeArg(a, "from"), nodeArg(a, "to"), slotArg(a)); } });
    actions.add({ .id = Actions::LinkDisconnect, .title = "Disconnect", .kind = K::DocumentEdit, .args = { Arg::id("to"), slotSpec() },
                  .run = [&p](const ActionArgs& a) { return p.disconnect(nodeArg(a, "to"), slotArg(a)); } });
    actions.add({ .id = Actions::LayerMove, .title = "Move Layer", .kind = K::DocumentEdit,
                  .args = { Arg::id("node"), Arg::integer("from", 0), Arg::integer("to", 0) }, .run = [&p](const ActionArgs& a) {
        return p.moveLayer(nodeArg(a, "node"), size_t(a.integer("from")), size_t(a.integer("to")));
    } });

    // ---------------------------------------------------------------- values
    actions.add({ .id = Actions::SignalSet, .title = "Set Signal", .kind = K::DocumentEdit,
                  .args = { Arg::id("node"), signalArg(), channelArg(), Arg::number("value", 0.0, 1.0) }, .run = [&p](const ActionArgs& a) {
        return p.setSignalScale(nodeArg(a, "node"), NodeSignal(a.integer("signal")), NodeChannel(a.integer("channel")), float(a.number("value")));
    } });
    actions.add({ .id = Actions::MoveSet, .title = "Set Offset", .kind = K::DocumentEdit,
                  .args = { Arg::id("node"), offsetArg("x"), offsetArg("y") }, .run = [&p](const ActionArgs& a) {
        return p.setNodeSettings(nodeArg(a, "node"), MoveSettings{ int(a.integer("x")), int(a.integer("y")) });
    } });

    // ---------------------------------------------------------------- import
    // target: where it was dropped (x y: a canvas point, or the node's place in the graph).
    // Payload: the tiles, already where they go.
    actions.add({ .id = Actions::ImageImport, .title = "Import Image", .kind = K::DocumentEdit,
                  .args = { Arg::text("path"), Arg::text("name"), Arg::choice("target", { "default", "canvas", "graph" }),
                            Arg::number("x"), Arg::number("y") },
                  .run = [&p](const ActionArgs& a) -> ActionResult {
        const TileGrid* const* tiles = a.payloadAs<const TileGrid*>();
        if (!tiles || !*tiles) return {};
        Project::ImportOptions options;
        if (a.text("target") == "graph") {   // a node where it was dropped, linked to nothing
            options.addToMaster = false;
            options.makeActive  = false;
            options.placeNode   = true;
            options.nodeX = float(a.number("x"));
            options.nodeY = float(a.number("y"));
        }
        const NodeId id = p.importTiles(**tiles, a.text("name"), options);
        return { id != kNoNodeId, int64_t(id) };
    } });

    // ---------------------------------------------------------------- what the views share
    // Not the document: never undone, never logged. The canvas and the graph follow these.
    const auto ui = K::Interface;
    // nodes: the new selection (none: nothing); anchor: what Shift+click chains from (0: none).
    actions.add({ .id = Actions::SelectionSet, .title = "Select", .kind = ui, .args = { Arg::ids("nodes"), Arg::integer("anchor", 0) },   // selectionArgs()
                  .run = [&p](const ActionArgs& a) {
        NodeSelection::Set ids;
        for (int64_t i : a.list("nodes")) ids.insert(NodeId(i));
        return p.setSelection({ std::move(ids), NodeId(a.integer("anchor")) });
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
        return a.has("x") ? Vec2{ float(a.number("x")), float(a.number("y")) } : Vec2{ viewport.x * 0.5f, viewport.y * 0.5f };
    };
    actions.add({ .id = Actions::CanvasViewport, .title = "Canvas Size", .kind = ui,
                  .args = { Arg::number("width", 0), Arg::number("height", 0) },
                  .run = [&p](const ActionArgs& a) { return p.setCanvasViewport({ float(a.number("width")), float(a.number("height")) }); } });
    actions.add({ .id = Actions::CanvasPan, .title = "Pan", .kind = ui, .args = { Arg::number("dx"), Arg::number("dy") },
                  .enabled = shown, .run = [camera](const ActionArgs& a) {
        return camera([&](CanvasCamera& c, Vec2) { c.panBy({ float(a.number("dx")), float(a.number("dy")) }); });
    } });
    actions.add({ .id = Actions::CanvasZoom, .title = "Zoom", .kind = ui,
                  .args = { Arg::number("factor", 1e-6), Arg::number("x").optional(), Arg::number("y").optional() },
                  .enabled = shown, .run = [camera, pointArg](const ActionArgs& a) {
        return camera([&](CanvasCamera& c, Vec2 viewport) { c.zoomAt(pointArg(a, viewport), float(a.number("factor"))); });
    } });
    actions.add({ .id = Actions::CanvasRotate, .title = "Rotate", .kind = ui,
                  .args = { Arg::number("degrees"), Arg::number("x").optional(), Arg::number("y").optional() },
                  .enabled = shown, .run = [camera, pointArg](const ActionArgs& a) {
        return camera([&](CanvasCamera& c, Vec2 viewport) { c.rotateAt(pointArg(a, viewport), toRadians(float(a.number("degrees")))); });
    } });
    actions.add({ .id = Actions::CanvasCenterOn, .title = "Center On", .kind = ui, .args = { Arg::number("x"), Arg::number("y") },
                  .enabled = shown, .run = [camera](const ActionArgs& a) {
        return camera([&](CanvasCamera& c, Vec2 viewport) { c.centerOn(float(a.number("x")), float(a.number("y")), viewport.x, viewport.y); });
    } });
    // A world rectangle, fitted (maxX < minX: none, the origin centred).
    actions.add({ .id = Actions::CanvasFrame, .title = "Frame", .kind = ui,
                  .args = { Arg::integer("minX"), Arg::integer("minY"), Arg::integer("maxX"), Arg::integer("maxY") },
                  .enabled = shown, .run = [camera](const ActionArgs& a) {
        const BoundsI b{ int(a.integer("minX")), int(a.integer("minY")), int(a.integer("maxX")), int(a.integer("maxY")) };
        return camera([&](CanvasCamera& c, Vec2 viewport) {
            if (b.empty()) c.centerOn(0.0f, 0.0f, viewport.x, viewport.y);
            else           c.frame(float(b.minX), float(b.minY), float(b.maxX + 1), float(b.maxY + 1), viewport.x, viewport.y);
        });
    } });
    actions.add({ .id = Actions::CanvasFrameAll, .title = "Frame All", .kind = ui, .shortcuts = { { SDLK_F } }, .scope = ActionScope::Canvas,
                  .enabled = shown, .run = [&actions, &p] {   // lost? back to the picture
        const BoundsI b = p.master().bounds();
        return actions.run(Actions::CanvasFrame, { { "minX", b.minX }, { "minY", b.minY }, { "maxX", b.maxX }, { "maxY", b.maxY } });
    } });

    // ---------------------------------------------------------------- gestures
    // A stroke into the active raster: one undo step. Its points stream in as they're drawn
    // (payloads: StrokeStart, then spans of StrokePoint); the rest is for the log.
    actions.add({ .id = Actions::BrushStroke, .title = "Brush Stroke", .kind = K::Gesture,
                  .args = { Arg::integer("raster", 0), Arg::choice("mode", { "paint", "erase" }), Arg::number("size", 0),
                            Arg::text("color"), Arg::choice("device", { "mouse", "pen" }) },
                  .endArgs = { Arg::integer("points", 0) },
                  .begin = [&p](const ActionArgs& a) {
                      const StrokeStart* s = a.payloadAs<StrokeStart>();
                      if (!s || !p.activeRasterEditable()) return false;
                      p.beginStroke(s->params, s->at);
                      return true;
                  },
                  .update = [&p](const ActionArgs& a) {
                      if (const auto* path = a.payloadAs<std::span<const StrokePoint>>()) p.strokeTo(*path);
                  },
                  .end    = [&p] { p.endStroke(); },
                  .cancel = [&p] { p.endStroke(); } });

    // Nodes dragged in the graph: dx dy from where they were. Previewed while dragging (the graph
    // shows them there), moved once at the end.
    auto dragged = std::make_shared<std::vector<NodeId>>();
    const auto dragTo = [&p, dragged](const ActionArgs& a) {
        return p.previewEdit(NodesMoveEdit{ *dragged, float(a.number("dx")), float(a.number("dy")) });
    };
    actions.add({ .id = Actions::NodeDrag, .title = "Move Nodes", .kind = K::Gesture,
                  .args = { Arg::ids("nodes") }, .endArgs = { Arg::number("dx"), Arg::number("dy") },
                  .updateArgs = { Arg::number("dx"), Arg::number("dy") },
                  .begin = [&p, dragged](const ActionArgs& a) {
                      dragged->clear();
                      for (int64_t i : a.list("nodes"))
                          if (p.find(NodeId(i))) dragged->push_back(NodeId(i));
                      return !dragged->empty();
                  },
                  .update = dragTo,
                  .end    = [&p, dragTo](const ActionArgs& a) { if (dragTo(a)) p.applyPreview(); },
                  .cancel = [&p] { p.cancelPreview(); } });

    // A value dragged on a node: previewed while dragging, applied once at the end.
    const auto previewScale = [&p](const ActionArgs& a) {
        return p.previewEdit(ScaleEdit{ nodeArg(a, "node"), NodeSignal(a.integer("signal")), NodeChannel(a.integer("channel")), float(a.number("value")) });
    };
    const auto previewOffset = [&p](const ActionArgs& a) {
        return p.previewEdit(OffsetEdit{ nodeArg(a, "node"), int(a.integer("x")), int(a.integer("y")) });
    };
    actions.add({ .id = Actions::SignalScrub, .title = "Change Signal", .kind = K::Gesture,
                  .args = { Arg::id("node"), signalArg(), channelArg() },
                  .endArgs = { Arg::id("node"), signalArg(), channelArg(), Arg::number("value", 0.0, 1.0) },
                  .updateArgs = { Arg::id("node"), signalArg(), channelArg(), Arg::number("value", 0.0, 1.0) },
                  .begin  = [&p](const ActionArgs& a) { return p.find(nodeArg(a, "node")) != nullptr; },
                  .update = previewScale,
                  .end    = [&p, previewScale](const ActionArgs& a) { if (previewScale(a)) p.applyPreview(); },
                  .cancel = [&p] { p.cancelPreview(); } });
    actions.add({ .id = Actions::MoveScrub, .title = "Change Offset", .kind = K::Gesture,
                  .args = { Arg::id("node") }, .endArgs = { Arg::id("node"), offsetArg("x"), offsetArg("y") },
                  .updateArgs = { Arg::id("node"), offsetArg("x"), offsetArg("y") },
                  .begin  = [&p](const ActionArgs& a) { return p.find(nodeArg(a, "node")) != nullptr; },
                  .update = previewOffset,
                  .end    = [&p, previewOffset](const ActionArgs& a) { if (previewOffset(a)) p.applyPreview(); },
                  .cancel = [&p] { p.cancelPreview(); } });

    // A link lifted off an input (source from slot) and carried, previewed: dropped on an input (to
    // toSlot) it moves there, dropped on nothing it comes off. Cancelled, nothing changes.
    actions.add({ .id = Actions::LinkMove, .title = "Move Link", .kind = K::Gesture,
                  .args = { Arg::id("source"), Arg::id("from"), slotSpec() },
                  .endArgs = { Arg::id("to").optional(), slotSpec("toSlot").optional(), Arg::flag("dropped").optional() },
                  .begin = [&p](const ActionArgs& a) { return p.previewEdit(LinkMoveEdit{ nodeArg(a, "source"), nodeArg(a, "from"), slotArg(a) }); },
                  .end   = [&p](const ActionArgs& a) {
                      const auto* lifted = p.preview() ? p.preview()->as<LinkMoveEdit>() : nullptr;
                      if (!lifted) return false;
                      const LinkMoveEdit link = *lifted;
                      p.cancelPreview();
                      p.disconnect(link.from, link.slot);
                      return !a.has("to") || p.connect(link.source, nodeArg(a, "to"), slotArg(a, "toSlot"));
                  },
                  .cancel = [&p] { p.cancelPreview(); } });
}

ActionArgs selectionArgs(const NodeSelection& selection)
{
    return { { "nodes", selection.list() }, { "anchor", selection.anchor() } };
}

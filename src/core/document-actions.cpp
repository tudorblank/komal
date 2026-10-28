#include "core/document-actions.hpp"
#include "core/project.hpp"

#include <SDL3/SDL_keycode.h>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace {

constexpr const char* kTypeNames[] = { "raster", "reference", "move", "compositor" };   // NodeType order

NodeType parseNodeType(std::string_view name)
{
    for (int i = 0; i < 4; ++i)
        if (name == kTypeNames[i]) return NodeType(i);
    return NodeType::Raster;
}

NodeId nodeArg(const ActionArgs& a, std::string_view name) { return NodeId(a.integer(name, kNoNodeId)); }
InputSlot slotArg(const ActionArgs& a, std::string_view name = "slot") { return InputSlot::parse(a.text(name)); }

}

const char* nodeTypeName(NodeType type) { return kTypeNames[int(type)]; }

void registerDocumentActions(ActionRegistry& actions, Project& project)
{
    using K = ActionKind;
    Project& p = project;

    // Every document action below is one undo step, named by its title.
    actions.setUndoSteps([&p](const Action& a) { p.beginStep(a.title); }, [&p] { p.endStep(); });

    // ---------------------------------------------------------------- history (moving through it isn't a step)
    // Primary is Cmd on macOS, Ctrl elsewhere.
    actions.add({ .id = "edit.undo", .title = "Undo", .kind = K::DocumentEdit, .shortcuts = { { SDLK_Z, ModPrimary } },
                  .repeats = true, .undoStep = false, .enabled = [&p] { return p.canUndo(); }, .run = [&p] { return p.undo(); } });
    actions.add({ .id = "edit.redo", .title = "Redo", .kind = K::DocumentEdit,
                  .shortcuts = { { SDLK_Z, ModPrimary | ModShift }, { SDLK_Y, ModPrimary } },
                  .repeats = true, .undoStep = false, .enabled = [&p] { return p.canRedo(); }, .run = [&p] { return p.redo(); } });
    // node: any step of the history tree, across branches (the History Tree panel).
    actions.add({ .id = "history.goTo", .title = "Go to History Step", .kind = K::DocumentEdit, .undoStep = false,
                  .enabled = [&p] { return !p.strokeActive() && p.history().size() > 1; },
                  .run = [&p](const ActionArgs& a) { return p.goToHistory(uint32_t(a.integer("node"))); } });

    // ---------------------------------------------------------------- nodes
    // type x y, and optionally: from (fed into the new node's natural input), into + slot (fed by it).
    actions.add({ .id = "node.add", .title = "Add Node", .kind = K::DocumentEdit, .run = [&p](const ActionArgs& a) -> ActionResult {
        const NodeId id = p.createNode(parseNodeType(a.text("type")), float(a.number("x")), float(a.number("y")));
        const Node* n = p.find(id);
        if (!n) return {};
        InputSlot in;
        const bool hasInput = preferredInput(*n, in);
        bool fed = !a.has("from");
        if (!fed && hasInput) fed = p.connect(nodeArg(a, "from"), id, in);
        if (a.has("into") && fed) p.connect(id, nodeArg(a, "into"), slotArg(a));
        return { true, id };
    } });
    actions.add({ .id = "node.reference", .title = "Add Reference", .kind = K::DocumentEdit, .run = [&p](const ActionArgs& a) -> ActionResult {
        const NodeId id = p.createReference(float(a.number("x")), float(a.number("y")), nodeArg(a, "node"));
        return { id != kNoNodeId, id };
    } });
    actions.add({ .id = "node.delete", .title = "Delete Nodes", .kind = K::DocumentEdit, .run = [&p](const ActionArgs& a) {
        std::vector<NodeId> ids;
        for (int64_t i : a.list("nodes")) ids.push_back(NodeId(i));
        return p.deleteNodes(ids);
    } });
    actions.add({ .id = "node.rename", .title = "Rename", .kind = K::DocumentEdit, .run = [&p](const ActionArgs& a) {
        p.setNodeLabel(nodeArg(a, "node"), a.text("label"));
    } });

    // ---------------------------------------------------------------- rasters
    // Which raster the canvas paints into isn't history (like picking a layer).
    actions.add({ .id = "raster.activate", .title = "Paint into This Raster", .kind = K::DocumentEdit, .undoStep = false,
                  .run = [&p](const ActionArgs& a) {
        p.setActiveRaster(nodeArg(a, "node"));
    } });
    actions.add({ .id = "raster.lock", .title = "Lock Pixels", .kind = K::DocumentEdit, .run = [&p](const ActionArgs& a) {
        p.setRasterLocked(nodeArg(a, "node"), a.flag("on"));
    } });
    actions.add({ .id = "raster.lockAlpha", .title = "Lock Transparency", .kind = K::DocumentEdit, .run = [&p](const ActionArgs& a) {
        p.setRasterAlphaLocked(nodeArg(a, "node"), a.flag("on"));
    } });

    // ---------------------------------------------------------------- links
    actions.add({ .id = "link.connect", .title = "Connect", .kind = K::DocumentEdit, .run = [&p](const ActionArgs& a) {
        return p.connect(nodeArg(a, "from"), nodeArg(a, "to"), slotArg(a));
    } });
    actions.add({ .id = "link.disconnect", .title = "Disconnect", .kind = K::DocumentEdit, .run = [&p](const ActionArgs& a) {
        return p.disconnect(nodeArg(a, "to"), slotArg(a));
    } });
    actions.add({ .id = "layer.move", .title = "Move Layer", .kind = K::DocumentEdit, .run = [&p](const ActionArgs& a) {
        return p.moveLayer(nodeArg(a, "node"), size_t(a.integer("from")), size_t(a.integer("to")));
    } });

    // ---------------------------------------------------------------- values
    actions.add({ .id = "signal.set", .title = "Set Signal", .kind = K::DocumentEdit, .run = [&p](const ActionArgs& a) {
        p.setSignalScale(nodeArg(a, "node"), NodeSignal(a.integer("signal")), NodeChannel(a.integer("channel")), float(a.number("value")));
    } });
    actions.add({ .id = "move.set", .title = "Set Offset", .kind = K::DocumentEdit, .run = [&p](const ActionArgs& a) {
        p.setMoveOffset(nodeArg(a, "node"), int(a.integer("x")), int(a.integer("y")));
    } });

    // ---------------------------------------------------------------- import
    // path name target (canvas, graph or default) x y; payload: the tiles, already where they go.
    actions.add({ .id = "image.import", .title = "Import Image", .kind = K::DocumentEdit, .run = [&p](const ActionArgs& a) -> ActionResult {
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
        return { id != kNoNodeId, id };
    } });

    // ---------------------------------------------------------------- gestures
    // A stroke into the active raster: one undo step. Its points stream in as they're drawn.
    actions.add({ .id = "brush.stroke", .title = "Brush Stroke", .kind = K::Gesture,
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

    // Nodes dragged in the graph: begin with nodes, then dx dy from where they were.
    auto dragStart = std::make_shared<std::vector<std::pair<NodeId, std::pair<float, float>>>>();
    const auto dragTo = [&p, dragStart](const ActionArgs& a) {
        const float dx = float(a.number("dx")), dy = float(a.number("dy"));
        for (const auto& [id, at] : *dragStart) p.setNodePosition(id, at.first + dx, at.second + dy);
    };
    actions.add({ .id = "node.drag", .title = "Move Nodes", .kind = K::Gesture,
                  .begin = [&p, dragStart](const ActionArgs& a) {
                      dragStart->clear();
                      for (int64_t i : a.list("nodes"))
                          if (const Node* n = p.find(NodeId(i))) dragStart->push_back({ NodeId(i), { n->m_meta.x, n->m_meta.y } });
                      return !dragStart->empty();
                  },
                  .update = dragTo, .end = dragTo });

    // A value dragged on a node: previewed while dragging, applied once at the end.
    //   signal.scrub   node signal channel value
    //   move.scrub     node x y
    const auto previewScale = [&p](const ActionArgs& a) {
        EditPreview e;
        e.kind    = EditPreview::Kind::SignalScale;
        e.node    = nodeArg(a, "node");
        e.signal  = NodeSignal(a.integer("signal"));
        e.channel = NodeChannel(a.integer("channel"));
        e.scale   = float(a.number("value"));
        p.previewEdit(e);
    };
    const auto previewOffset = [&p](const ActionArgs& a) {
        EditPreview e;
        e.kind = EditPreview::Kind::MoveOffset;
        e.node = nodeArg(a, "node");
        e.x    = int(a.integer("x"));
        e.y    = int(a.integer("y"));
        p.previewEdit(e);
    };
    actions.add({ .id = "signal.scrub", .title = "Change Signal", .kind = K::Gesture,
                  .begin  = [&p](const ActionArgs& a) { return p.find(nodeArg(a, "node")) != nullptr; },
                  .update = previewScale,
                  .end    = [&p, previewScale](const ActionArgs& a) { previewScale(a); p.applyPreview(); },
                  .cancel = [&p] { p.cancelPreview(); } });
    actions.add({ .id = "move.scrub", .title = "Change Offset", .kind = K::Gesture,
                  .begin  = [&p](const ActionArgs& a) { return p.find(nodeArg(a, "node")) != nullptr; },
                  .update = previewOffset,
                  .end    = [&p, previewOffset](const ActionArgs& a) { previewOffset(a); p.applyPreview(); },
                  .cancel = [&p] { p.cancelPreview(); } });

    // A link lifted off an input (source from slot) and carried: dropped on an input (to slot) it
    // connects there, dropped on nothing it stays off.
    actions.add({ .id = "link.move", .title = "Move Link", .kind = K::Gesture,
                  .begin = [&p](const ActionArgs& a) { return p.disconnect(nodeArg(a, "from"), slotArg(a)); },
                  .end   = [&p](const ActionArgs& a) {
                      if (!a.has("to")) return true;
                      return p.connect(nodeArg(a, "source"), nodeArg(a, "to"), slotArg(a, "toSlot"));
                  } });
}

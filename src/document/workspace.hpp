#pragma once
// The document: a graph of nodes, changed only through this API (an action, then a call here),
// every change told right after it's made (`changed`). Built from blocks, each its own small world:
//
//   NodeRegistry    the nodes, by id and in creation order, and every raster's data
//   Links           the document's rules for links (the nodes' own: Node::link)
//   GraphState      the graph as history keeps it: captured when a step ends, restored by undo
//   StepRecorder    the steps: the History tree, the step open now, what changed since a capture
//   StrokeSession   the brush stroke being painted: its coverage, until it's committed
//   EditPreview     the edit in progress (a drag or a stroke), shown on the GPU until it's applied
//
// History knows the graph changed by the nodes graph events named: a step that leaves the graph
// alone (a stroke) captures nothing and shares the last step's state.

#include "base/signal.hpp"
#include "document/artboard.hpp"
#include "document/canvas-camera.hpp"
#include "document/edit-preview.hpp"
#include "document/links.hpp"
#include "document/node-registry.hpp"
#include "document/node-selection.hpp"
#include "document/workspace-event.hpp"
#include "document/workspace-file.hpp"
#include "document/stack-rows.hpp"
#include "document/step-recorder.hpp"
#include "document/stroke-session.hpp"
#include "node/node-kinds.hpp"
#include "raster/brush.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

class Workspace {
public:
    // A blank workspace: the master alone. `seed`, if given, adds what it opens with (a
    // file's content, a test's starter graph) before the history starts: undo never goes past it.
    void init(const std::function<void(Workspace&)>& seed = {});
    // ---- files (document/workspace-saved.cpp; the format: document/workspace-file.hpp)
    // In place of init(): a saved workspace, as it was saved (history starts there). False, with
    // why, if it doesn't fit together (a link that can't be, settings not its kind's): what's
    // built of it then is to be thrown away.
    bool open(const WorkspaceFile& file, std::string& error);
    WorkspaceFile saved() const;   // everything a file keeps (not history), pixels copied

    // ---- read
    const std::vector<NodePtr>& nodes() const { return m_nodes.nodes(); }   // creation order
    Node*   find(NodeId id) const { return m_nodes.find(id); }
    NodePtr findPtr(NodeId id) const { return m_nodes.findPtr(id); }
    CompositorNode& master() const { return *m_master; }
    bool    isMaster(NodeId id) const { return m_master && m_master->id() == id; }
    RasterRootNode* activeRaster() const;
    NodeId  activeRasterId() const { return m_active; }
    NodePtr inputAt(NodeId to, const InputSlot& slot) const;   // what feeds that slot, if anything
    bool    preferredInput(NodeId id, InputSlot& out) const;   // where a link dropped on its body goes (none: false)

    // ---- graph edits: true when they changed something (nothing to change, or not allowed: false)
    // A new node, named after its kind (the first raster becomes the active one), wired as asked:
    // fed by `from` (into its natural input), feeding `into` at `slot`. All of it, or nothing (kNoNodeId).
    struct NodeWiring {
        NodeId    from = kNoNodeId;
        NodeId    into = kNoNodeId;
        InputSlot slot;
    };
    NodeId addNode(NodeType type, float x, float y, const NodeWiring& wiring);
    // A new raster node holding an image, already cut into tiles where they go (tilesFromImage,
    // any thread). Its pixels are a raster commit of the step it's made in.
    struct ImportOptions {
        bool  addToMaster = true;    // as the master's new top layer
        bool  makeActive  = true;    // the raster the canvas paints into
        bool  placeNode   = false;   // at (nodeX, nodeY) in the graph; else below the lowest raster
        float nodeX = 0.0f, nodeY = 0.0f;
    };
    NodeId importTiles(const TileGrid& tiles, const std::string& name, const ImportOptions& options);
    // Deletes nodes and heals the graph: what a deleted node fed is re-fed from upstream, through
    // deleted nodes with exactly one input. Others leave their users unconnected. Never the master.
    bool   deleteNodes(const std::vector<NodeId>& ids);
    bool   setNodePosition(NodeId id, float x, float y);
    bool   setNodeLabel(NodeId id, std::string label);
    bool   setActiveRaster(NodeId id);
    // Its kind's own settings (NodeSettings: a Move's offset, a raster's locks, a stack's mutes);
    // refused if they aren't its kind's or don't fit (an offset out of the world, a stack's groups:
    // those are the artboards'). A stroke on it ends first: it keeps the rules it started with.
    bool   setNodeSettings(NodeId id, const NodeSettings& settings);

    bool canConnect(NodeId from, NodeId to, const InputSlot& slot) const;
    bool connect(NodeId from, NodeId to, const InputSlot& slot);
    bool disconnect(NodeId to, const InputSlot& slot);
    // The link feeding `slot` of `from` (from `source`), moved: onto `toSlot` of `to`, or (to
    // kNoNodeId) off. Checked first, on the graph without it (Links::canMove): all of it, or nothing.
    bool canMoveLink(NodeId source, NodeId from, const InputSlot& slot, NodeId to, const InputSlot& toSlot) const;
    bool moveLink(NodeId source, NodeId from, const InputSlot& slot, NodeId to, const InputSlot& toSlot);
    // ---- a stack's rows (document/workspace-stacks.cpp)
    // A layer left out of a compositor's composite, there only (it shows anywhere else it's used).
    bool setLayerMuted(NodeId compositor, NodeId layer, bool muted);
    // Layers, and the master's artboards whole, moved together to one place: see StackMove.
    bool moveStackRows(NodeId compositor, const StackMove& move);
    // `source` into a compositor's stack at a place (into an artboard, at a position among the rows
    // there): linked on top, then moved there.
    bool addLayer(NodeId source, NodeId compositor, ArtboardId into, size_t at);
    // Rows taken out of a stack: its layers unlinked (the nodes stay in the graph), the master's
    // artboards deleted (their layers stay, in no artboard).
    bool removeStackRows(NodeId compositor, const std::vector<NodeId>& layers, const std::vector<ArtboardId>& artboards);

    // ---- the master's artboards (document/artboard.hpp): each a group of the master's stack
    const Artboards& artboards() const { return m_artboards; }
    const Artboard*  artboard(ArtboardId id) const;
    // A new one, empty and transparent, on top of the master's stack (kNoArtboard: an empty rect,
    // or out of the world).
    ArtboardId addArtboard(const BoundsI& rect);
    bool setArtboard(const Artboard& a);   // the one with its id: its name, rect, colour and eye
    bool deleteArtboard(ArtboardId id);    // its layers stay where they are, in no artboard
    // Each artboard is exactly one of the master's groups and each group an artboard: held by every
    // edit above and by history (asserted where the master is shown them; the tests check it).
    bool artboardsMatchStack() const;

    bool setSignalScale(NodeId id, NodeSignal s, NodeChannel c, float v);

    // ---- edits in progress (see EditPreview): the graph waits, the views preview them on the GPU
    // Starts or updates one (not a stroke: beginStroke()); false if it doesn't fit the document
    // (a node that isn't there, a value out of range). A stroke going on ends first.
    bool previewEdit(const EditPreview& e);
    void applyPreview();   // a dragged value or nodes moved: now a real change
    void cancelPreview();
    const EditPreview* preview() const { return m_preview ? &*m_preview : nullptr; }   // null: none

    // ---- brush strokes on the active raster: only the stroke's coverage grows while painting
    // (previewed). endStroke() composes it over the raster: one raster commit, one history step.
    // cancelStroke() drops it: the raster never changed.
    void beginStroke(const BrushParams& params, StrokePoint at);
    void strokeTo(std::span<const StrokePoint> path);   // through every point
    void endStroke();
    void cancelStroke();
    bool strokeActive() const { return m_stroke.active(); }
    const StrokeSession& stroke() const { return m_stroke; }   // its coverage and brush, for the preview
    bool activeRasterEditable() const;   // there's an active raster and it takes strokes

    // ---- history: a tree of steps (document/history). Every document action is one step: the action
    // layer opens it before the action runs and closes it after it ends; steps opened inside one
    // merge into it, and a step that changed nothing isn't kept. The active raster isn't history.
    void beginStep(std::string label);
    void endStep();
    bool canUndo() const { return !m_stroke.active() && !m_steps.open() && history().canUndo(); }
    bool canRedo() const { return !m_stroke.active() && !m_steps.open() && history().canRedo(); }
    bool undo();
    bool redo();
    bool goToHistory(StepId step);   // any step of the tree, across branches
    // Steps gone for good (Start never): each one's children take its place. Deleting the current
    // step moves to the nearest step above it that stays first. Not a step itself: no undo.
    bool deleteHistory(const std::vector<StepId>& steps);
    const History& history() const { return m_steps.history(); }
    NodeRegistry::PixelBytes pixelBytes() const { return m_nodes.pixelBytes(); }   // the Info panel's memory line
    // History's pixels kept raw near where it is (every step within kHotSteps of the current one,
    // up or down the tree) and cold (their encoded bytes) everywhere else: once history moved or
    // grew, a little at a time, never in the move itself. Upkeep, not an edit: nothing is told.
    // False while there's more to do.
    static constexpr int kHotSteps = 8;
    bool settleHistory(const Deadline& until);
    bool historySettled() const { return m_historySettled; }

    // ---- what the views share, so none of them changes another: the canvas and the graph follow
    // these through events. Changed only by actions (selection.set, canvas.*); never undone.
    const NodeSelection& selection() const { return m_selection; }
    bool setSelection(NodeSelection selection);   // nodes that don't exist are left out
    const CanvasCamera& canvasCamera() const { return m_canvasCamera; }
    Vec2 canvasViewport() const { return m_canvasViewport; }   // the canvas's size, points ({0, 0}: not shown)
    bool setCanvasCamera(const CanvasCamera& camera);
    bool setCanvasViewport(Vec2 size);

    // ---- events: every change, told right after it's made (see WorkspaceEvent). Only Workspace emits.
    Signal<Workspace, const WorkspaceEvent&> changed;

private:
    static GraphKey key() { return {}; }   // lent to the blocks that edit the graph
    NodeId createNode(NodeType type, float x, float y);   // named after its kind; the first raster becomes the active one
    RasterRootNode* rasterAt(NodeId id) const;   // the raster node with that id (null: none, or not a raster)
    void emit(WorkspaceEventType t, NodeId n = kNoNodeId) { emit(WorkspaceEvent{ t, n, kNoNodeId, {} }); }
    void emit(const WorkspaceEvent& e);
    EmitEvent emitter() { return [this](const WorkspaceEvent& e) { emit(e); }; }   // for the blocks that change the graph
    void emitInputChanges(NodeId to, const NodeLinks& before);   // ... against its links now
    bool previewTouches(const absl::flat_hash_set<NodeId>& nodes) const;   // the edit in progress names one of them
    void restoreRasters(const RasterStates& rasters);   // every raster as `rasters` holds it (those there then)
    void pixelsChanged(const char* label);   // into the open step's, or a step of their own
    void restoreGraph(const GraphState& target);
    void fallBackActiveRaster();   // the active raster went: the most recently made one, if any
    void showArtboards();          // the master draws them as they are (CompositorNode::setLooks)
    bool setMasterGroups(std::vector<StackGroup> groups);

    NodeRegistry m_nodes;   // first, destroyed last: everything else holds its nodes
    std::shared_ptr<CompositorNode> m_master;
    NodeId m_active = kNoNodeId;

    Artboards  m_artboards;   // before the history, which reads them
    ArtboardId m_lastArtboard = kNoArtboard;   // ids never come back
    StepRecorder m_steps{ m_nodes, m_artboards };
    bool m_seeding = false;   // init()'s seed runs: its edits are the start, no step of their own

    NodeSelection m_selection;
    CanvasCamera  m_canvasCamera;
    Vec2          m_canvasViewport{};

    bool m_historySettled = true;   // history's pixels are hot and cold as settleHistory leaves them

    StrokeSession m_stroke;
    std::optional<EditPreview> m_preview;
};

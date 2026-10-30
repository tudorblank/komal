#pragma once
// The document: a graph of nodes, changed only through this API (an action, then a call here),
// every change told right after it's made (`changed`). Built from blocks, each its own small world:
//
//   NodeRegistry    the nodes, by id and in creation order, and every raster's data
//   Links           what feeds a node, and the rules for linking (NodeInputs)
//   GraphState      the graph as history keeps it: captured when a step ends, restored by undo
//   History         the tree of steps: raster commits and graph states
//   StrokeSession   the brush stroke being painted: its coverage, until it's committed
//   EditPreview     the edit in progress (a drag or a stroke), shown on the GPU until it's applied
//
// History knows the graph changed by a revision every graph event bumps: a step that leaves the
// graph alone (a stroke) captures nothing and shares the last step's state.

#include "base/signal.hpp"
#include "document/canvas-camera.hpp"
#include "document/edit-preview.hpp"
#include "document/graph-state.hpp"
#include "document/history.hpp"
#include "document/image-load.hpp"
#include "document/input-slot.hpp"
#include "document/links.hpp"
#include "document/node-registry.hpp"
#include "document/node-selection.hpp"
#include "document/project-event.hpp"
#include "document/stroke-session.hpp"
#include "node/node-kinds.hpp"
#include "raster/brush.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>

class Project {
public:
    void init();   // master output plus a starter graph

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
    NodeId createNode(NodeType type, float x, float y);   // named after its kind; the first raster becomes the active one
    // A new raster node holding an image, already cut into tiles where they go (tilesFromImage,
    // any thread). Not undoable.
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
    // Its kind's own settings (NodeSettings: a Move's offset, a raster's locks); refused if they
    // aren't its kind's or don't fit (an offset out of the world). A stroke on it ends first: it
    // keeps the rules it started with.
    bool   setNodeSettings(NodeId id, const NodeSettings& settings);

    bool canConnect(NodeId from, NodeId to, const InputSlot& slot) const;
    bool connect(NodeId from, NodeId to, const InputSlot& slot);
    bool disconnect(NodeId to, const InputSlot& slot);
    bool moveLayer(NodeId compositor, size_t from, size_t to);

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
    void beginStroke(const BrushParams& params, StrokePoint at);
    void strokeTo(std::span<const StrokePoint> path);   // through every point
    void endStroke();
    bool strokeActive() const { return m_stroke.active(); }
    const StrokeSession& stroke() const { return m_stroke; }   // its coverage and brush, for the preview
    bool activeRasterEditable() const;   // there's an active raster and it takes strokes

    // ---- history: a tree of steps (document/history). Every document action is one step: the action
    // layer opens it before the action runs and closes it after it ends; steps opened inside one
    // merge into it, and a step that changed nothing isn't kept. The active raster isn't history.
    void beginStep(std::string label);
    void endStep();
    bool canUndo() const { return !m_stroke.active() && m_stepDepth == 0 && m_history.canUndo(); }
    bool canRedo() const { return !m_stroke.active() && m_stepDepth == 0 && m_history.canRedo(); }
    bool undo();
    bool redo();
    bool goToHistory(StepId step);   // any step of the tree, across branches
    const History& history() const { return m_history; }

    // ---- what the views share, so none of them changes another: the canvas and the graph follow
    // these through events. Changed only by actions (selection.set, canvas.*); never undone.
    const NodeSelection& selection() const { return m_selection; }
    bool setSelection(NodeSelection selection);   // nodes that don't exist are left out
    const CanvasCamera& canvasCamera() const { return m_canvasCamera; }
    Vec2 canvasViewport() const { return m_canvasViewport; }   // the canvas's size, points ({0, 0}: not shown)
    bool setCanvasCamera(const CanvasCamera& camera);
    bool setCanvasViewport(Vec2 size);

    // ---- events: every change, told right after it's made (see ProjectEvent). Only Project emits.
    Signal<Project, const ProjectEvent&> changed;

private:
    static GraphKey key() { return {}; }   // lent to the blocks that edit the graph
    RasterRootNode* rasterAt(NodeId id) const;   // the raster node with that id (null: none, or not a raster)
    void emit(ProjectEventType t, NodeId n = kNoNodeId) { emit(ProjectEvent{ t, n, kNoNodeId, {} }); }
    void emit(const ProjectEvent& e);
    EmitEvent emitter() { return [this](const ProjectEvent& e) { emit(e); }; }   // for the blocks that change the graph
    void emitInputChanges(NodeId to, const NodeInputs& before);   // ... against its inputs now
    bool previewTouches(const absl::flat_hash_set<NodeId>& nodes) const;   // the edit in progress names one of them
    void flipRasterCommit(const RasterCommitRef& c);
    void restoreGraph(const GraphState& target);
    void fallBackActiveRaster();   // the active raster went: the most recently made one, if any

    NodeRegistry m_nodes;   // first, destroyed last: everything else holds its nodes
    std::shared_ptr<CompositorNode> m_master;
    NodeId m_active = kNoNodeId;

    History m_history;
    int m_stepDepth = 0;                          // open steps (nested ones merge)
    std::string m_stepLabel;
    std::vector<RasterCommitRef> m_stepCommits;   // made while it's open
    std::shared_ptr<const GraphState> m_stepBefore;   // the graph when it opened
    uint64_t m_graphRevision   = 0;   // bumped by every event that changes what a GraphState holds
    uint64_t m_historyRevision = 0;   // ... when the current step's graph was last known to be the live one
    uint64_t m_stepRevision    = 0;   // ... when the open step opened

    NodeSelection m_selection;
    CanvasCamera  m_canvasCamera;
    Vec2          m_canvasViewport{};

    StrokeSession m_stroke;
    std::optional<EditPreview> m_preview;
};

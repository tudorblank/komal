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
#include "document/canvas-camera.hpp"
#include "document/edit-preview.hpp"
#include "document/links.hpp"
#include "document/node-registry.hpp"
#include "document/node-selection.hpp"
#include "document/project-event.hpp"
#include "document/step-recorder.hpp"
#include "document/stroke-session.hpp"
#include "node/node-kinds.hpp"
#include "raster/brush.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

class Project {
public:
    // A blank project: the master alone. `seed`, if given, adds what the project opens with (a
    // file's content, a test's starter graph) before the history starts: undo never goes past it.
    void init(const std::function<void(Project&)>& seed = {});

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
    // Its kind's own settings (NodeSettings: a Move's offset, a raster's locks); refused if they
    // aren't its kind's or don't fit (an offset out of the world). A stroke on it ends first: it
    // keeps the rules it started with.
    bool   setNodeSettings(NodeId id, const NodeSettings& settings);

    bool canConnect(NodeId from, NodeId to, const InputSlot& slot) const;
    bool connect(NodeId from, NodeId to, const InputSlot& slot);
    bool disconnect(NodeId to, const InputSlot& slot);
    // The link feeding `slot` of `from` (from `source`), moved: onto `toSlot` of `to`, or (to
    // kNoNodeId) off. Checked first, on the graph without it (Links::canMove): all of it, or nothing.
    bool canMoveLink(NodeId source, NodeId from, const InputSlot& slot, NodeId to, const InputSlot& toSlot) const;
    bool moveLink(NodeId source, NodeId from, const InputSlot& slot, NodeId to, const InputSlot& toSlot);
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
    const History& history() const { return m_steps.history(); }
    NodeRegistry::PixelBytes pixelBytes() const { return m_nodes.pixelBytes(); }   // the Info panel's memory line

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
    NodeId createNode(NodeType type, float x, float y);   // named after its kind; the first raster becomes the active one
    RasterRootNode* rasterAt(NodeId id) const;   // the raster node with that id (null: none, or not a raster)
    void emit(ProjectEventType t, NodeId n = kNoNodeId) { emit(ProjectEvent{ t, n, kNoNodeId, {} }); }
    void emit(const ProjectEvent& e);
    EmitEvent emitter() { return [this](const ProjectEvent& e) { emit(e); }; }   // for the blocks that change the graph
    void emitInputChanges(NodeId to, const NodeLinks& before);   // ... against its links now
    bool previewTouches(const absl::flat_hash_set<NodeId>& nodes) const;   // the edit in progress names one of them
    void flipRasterCommit(const RasterCommitRef& c);
    void recordCommit(const RasterCommitRef& commit, const char* label);   // into the open step (or one of its own)
    void restoreGraph(const GraphState& target);
    void fallBackActiveRaster();   // the active raster went: the most recently made one, if any

    NodeRegistry m_nodes;   // first, destroyed last: everything else holds its nodes
    std::shared_ptr<CompositorNode> m_master;
    NodeId m_active = kNoNodeId;

    StepRecorder m_steps{ m_nodes };
    bool m_seeding = false;   // init()'s seed runs: its edits are the start, no step of their own

    NodeSelection m_selection;
    CanvasCamera  m_canvasCamera;
    Vec2          m_canvasViewport{};

    StrokeSession m_stroke;
    std::optional<EditPreview> m_preview;
};

#pragma once

#include "core/history.hpp"
#include "core/image-load.hpp"
#include "node/node-types.hpp"
#include "raster/brush.hpp"

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Where a link lands on a node.
struct InputSlot {
    // Layer: an existing stack layer (connecting replaces it). NewLayer: a position (inserts there).
    enum class Kind : uint8_t { Input, Layer, NewLayer, Mask };
    Kind        kind    = Kind::Input;
    uint32_t    layer   = 0;                   // Layer: stack index, 0 = bottom; NewLayer: 0..count (count = on top)
    NodeSignal  signal  = NodeSignal::Main;    // Mask
    NodeChannel channel = NodeChannel::A;      // Mask

    static InputSlot input() { return {}; }
    static InputSlot stackLayer(uint32_t i) { return { Kind::Layer, i }; }
    static InputSlot newLayer(uint32_t at) { return { Kind::NewLayer, at }; }
    static InputSlot mask(NodeSignal s, NodeChannel c) { return { Kind::Mask, 0, s, c }; }

    bool operator==(const InputSlot& o) const
    {
        if (kind != o.kind) return false;
        if (kind == Kind::Layer || kind == Kind::NewLayer) return layer == o.layer;
        if (kind == Kind::Mask)  return signal == o.signal && channel == o.channel;
        return true;
    }

    // As actions write it: "input", "layer:2", "new:3", "mask:main.a" (and read it back).
    std::string text() const;
    static InputSlot parse(std::string_view text);
};

// The slot a link dropped on the node's body connects to (none: false).
bool preferredInput(const Node& n, InputSlot& out);

// What changed, told to every listener right after the change is made (the project's state is
// final by then). Views keep their own items and update them from these; nothing re-reads the
// whole project. Pixels travel separately, as dirty tiles through the node graph.
enum class ProjectEventType : uint8_t {
    NodeAdded,             // node
    NodeRemoved,           // node (its links are removed first)
    NodeMoved,             // node: its editor position
    NodeChanged,           // node: a value (signal scale, move offset, label, raster locks)
    LinkAdded,             // source feeds node at slot: a single input (Input, Mask)
    LinkRemoved,           // source no longer feeds node at slot
    LayersChanged,         // node: a stack's layers were added, removed, replaced or reordered
    PreviewChanged,        // an interactive edit started, moved, was applied or cancelled
    ActiveRasterChanged,
    RasterEdited,          // pixels of `node` changed (edit, undo, redo)
    HistoryChanged,
};
struct ProjectEvent {
    ProjectEventType type;
    NodeId    node   = kNoNodeId;
    NodeId    source = kNoNodeId;   // Link*
    InputSlot slot;                 // Link*
};

// An edit being dragged (a slider, an offset). While previewed the graph is untouched (nothing
// recomputes); views show the previewed value. applyPreview() makes it one real change.
struct EditPreview {
    enum class Kind : uint8_t { None, SignalScale, MoveOffset };
    Kind        kind    = Kind::None;
    NodeId      node    = kNoNodeId;
    NodeSignal  signal  = NodeSignal::Main;    // SignalScale
    NodeChannel channel = NodeChannel::A;
    float       scale   = 1.0f;
    int         x = 0, y = 0;                  // MoveOffset

    bool active() const { return kind != Kind::None; }
    bool isScale(NodeId n, NodeSignal s, NodeChannel c) const
    { return kind == Kind::SignalScale && node == n && signal == s && channel == c; }
};

// The graph as a history step leaves it: every node, its settings and its links. Undo and redo
// restore it; pixels travel separately, as raster commits. Each node type adds its own settings.
struct NodeState {
    NodePtr     node;   // the node itself: history keeps a deleted node alive, so it comes back as itself
    std::string label;
    float       x = 0.0f, y = 0.0f;
    float       scale[kSignalCount][kChannelCount] = {};
    NodeId      mask[kSignalCount][kChannelCount] = {};
    NodeId      input = kNoNodeId;                     // Reference, Move
    std::vector<NodeId> layers;                        // Compositor, bottom first
    int         offsetX = 0, offsetY = 0;              // Move (applied)
    bool        locked = false, alphaLocked = false;   // Raster
    bool operator==(const NodeState&) const = default;
};
struct GraphState {
    std::vector<NodeState> nodes;   // creation order
    bool operator==(const GraphState&) const = default;
};

class Project {
public:
    void init();   // master output plus a starter graph

    // ---- read
    const std::vector<NodePtr>& nodes() const { return m_order; }   // creation order
    Node*   find(NodeId id) const;
    NodePtr findPtr(NodeId id) const;
    CompositorNode& master() const { return *m_master; }
    bool    isMaster(NodeId id) const { return m_master && m_master->m_meta.id == id; }
    RasterRootNode* activeRaster() const;
    NodeId  activeRasterId() const { return m_active; }
    NodePtr inputAt(NodeId to, const InputSlot& slot) const;   // what feeds that slot, if anything

    // ---- graph edits
    NodeId createRaster(float x, float y);
    NodeId createReference(float x, float y, NodeId target = kNoNodeId);
    NodeId createMove(float x, float y);
    NodeId createCompositor(float x, float y);
    NodeId createNode(NodeType type, float x, float y);
    // A new raster node holding an image (not undoable). importImage: at the world origin;
    // importTiles: already cut into tiles where they go (tilesFromImage, any thread).
    struct ImportOptions {
        bool  addToMaster = true;    // as the master's new top layer
        bool  makeActive  = true;    // the raster the canvas paints into
        bool  placeNode   = false;   // at (nodeX, nodeY) in the graph; else below the lowest raster
        float nodeX = 0.0f, nodeY = 0.0f;
    };
    NodeId importImage(const ImageFile& image, const ImportOptions& options);
    NodeId importTiles(const TileGrid& tiles, const std::string& name, const ImportOptions& options);
    // Deletes nodes and heals the graph: what a deleted node fed is re-fed from upstream, through
    // deleted nodes with exactly one input. Others leave their users unconnected. Never the master.
    bool   deleteNodes(const std::vector<NodeId>& ids);
    bool   deleteNode(NodeId id) { return deleteNodes({ id }); }
    void   setNodePosition(NodeId id, float x, float y);
    void   setNodeLabel(NodeId id, std::string label);
    void   setActiveRaster(NodeId id);
    void   setRasterLocked(NodeId id, bool locked);        // a locked raster refuses pixel edits
    void   setRasterAlphaLocked(NodeId id, bool locked);   // strokes keep its transparency

    bool canConnect(NodeId from, NodeId to, const InputSlot& slot) const;
    bool connect(NodeId from, NodeId to, const InputSlot& slot);
    bool disconnect(NodeId to, const InputSlot& slot);
    bool moveLayer(NodeId compositor, size_t from, size_t to);

    void setSignalScale(NodeId id, NodeSignal s, NodeChannel c, float v);
    void setMoveOffset(NodeId id, int x, int y);

    // ---- interactive edits (see EditPreview)
    void previewEdit(const EditPreview& e);   // starts or updates the preview
    void applyPreview();                      // the previewed value becomes a real change
    void cancelPreview();
    const EditPreview& preview() const { return m_preview; }

    // ---- brush strokes on the active raster, into its draft (everything updates live).
    // endStroke() commits it: one raster commit, one history step.
    void beginStroke(const BrushParams& params, StrokePoint at);
    void strokeTo(std::span<const StrokePoint> path);   // through every point, applied once
    void endStroke();
    bool strokeActive() const { return m_stroke.active(); }
    bool activeRasterEditable() const;   // there's an active raster and it takes strokes

    // ---- history: a tree of steps (core/history). Every document action is one step: the action
    // layer opens it before the action runs and closes it after it ends; steps opened inside one
    // merge into it, and a step that changed nothing isn't kept. The active raster isn't history.
    void beginStep(std::string label);
    void endStep();
    bool canUndo() const { return !m_stroke.active() && m_stepDepth == 0 && m_history.canUndo(); }
    bool canRedo() const { return !m_stroke.active() && m_stepDepth == 0 && m_history.canRedo(); }
    bool undo();
    bool redo();
    bool goToHistory(uint32_t node);   // any step of the tree, across branches
    const History& history() const { return m_history; }
    size_t historyBytes() const;   // what the rasters keep for undo

    // ---- events (synchronous, in subscription order; listeners may (un)subscribe while called)
    using Listener = std::function<void(const ProjectEvent&)>;
    int  subscribe(Listener l);
    void unsubscribe(int token);

private:
    NodeId adopt(NodePtr n, float x, float y);   // registers a new node, emits NodeAdded
    NodePtr bridgeSource(const Node& n, const absl::flat_hash_set<NodeId>& deleted) const;
    RasterRootNode* rasterNode(RasterId id) const;
    void emit(ProjectEventType t, NodeId n = kNoNodeId) { emit(ProjectEvent{ t, n, kNoNodeId, {} }); }
    void emit(const ProjectEvent& e);

    // What feeds a node: snapshotted before an edit, compared after it to emit what changed.
    struct Inputs {
        NodePtr input;
        std::vector<NodePtr> layers;
        std::array<std::array<NodePtr, kChannelCount>, kSignalCount> masks;
        bool operator==(const Inputs&) const = default;
    };
    static Inputs inputsOf(const Node& n);
    void emitInputChanges(NodeId to, const Inputs& before, const Inputs& now);
    void emitInputChanges(NodeId to, const Inputs& before);   // ... against its inputs now
    void endPreviewBounds();   // the previewed node's bounds back to its applied state
    void applyStrokeChanges();
    RasterRootNode* strokeRaster() const;
    void flipRasterCommit(const RasterCommitRef& c);
    GraphState captureGraph() const;
    void restoreGraph(const GraphState& target);
    void fallBackActiveRaster();   // the active raster went: the most recently made one, if any

    // Declared first, destroyed last: raster root nodes point into it.
    std::deque<RasterData> m_rasters;   // RasterId == index; kept for history after node deletion

    std::vector<NodePtr> m_order;
    absl::flat_hash_map<NodeId, NodePtr> m_nodes;
    absl::flat_hash_map<RasterId, NodeId> m_rasterNodes;
    std::shared_ptr<CompositorNode> m_master;
    NodeId   m_active = kNoNodeId;
    NodeId   m_nextNodeId = 1;
    uint32_t m_rasterLabel = 0;   // "Raster N", never reused

    History m_history;
    int m_stepDepth = 0;                          // open steps (nested ones merge)
    std::string m_stepLabel;
    std::vector<RasterCommitRef> m_stepCommits;   // made while it's open
    GraphState m_stepBefore;
    BrushStroke m_stroke;          // the scratch strokes are painted into
    NodeId m_strokeNode = kNoNodeId;   // the raster it paints into

    EditPreview m_preview;

    struct Subscriber { int token; Listener listener; bool removed = false; };
    std::vector<std::unique_ptr<Subscriber>> m_listeners;  // stable addresses: safe to grow mid-emit
    int m_nextToken = 1;
    int m_emitting  = 0;                                    // emit() depth
};

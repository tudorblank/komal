#pragma once

#include "core/history.h"
#include "core/image-load.h"
#include "node/node-types.h"
#include "raster/brush.h"

#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

enum class ProjectEventType : uint8_t {
    NodeAdded,
    NodeRemoved,
    NodeMoved,             // editor position
    NodeChanged,           // signal scales, move offset, label, raster locks
    PreviewChanged,        // an interactive edit started, moved, was applied or cancelled
    GraphChanged,          // inputs, layers or masks of `node` changed
    ActiveRasterChanged,
    RasterEdited,          // pixels of `node` changed (edit, undo, redo)
    HistoryChanged,
};
struct ProjectEvent {
    ProjectEventType type;
    NodeId node = kNoNodeId;
};

// Where a link lands on a node.
struct InputSlot {
    // Layer is an existing layer of a stack (connecting replaces it); NewLayer is a position
    // in the stack (connecting inserts there, pushing the layers above it up).
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
};

// An edit being dragged (a slider, an offset field). While it is previewed the graph is left
// alone: no tile is recomputed and the canvas keeps its picture, while views show the
// previewed value (the node's control, an outline on the canvas). applyPreview() makes it
// one real change, when the drag ends.
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
    // A new raster node holding an image. Not undoable (creating nodes isn't). importImage puts
    // the image at the world origin; importTiles takes it already cut into tiles, wherever they
    // were placed (tilesFromImage, which can run on another thread).
    struct ImportOptions {
        bool  addToMaster = true;    // as the master's new top layer
        bool  makeActive  = true;    // the raster the canvas paints into
        bool  placeNode   = false;   // at (nodeX, nodeY) in the graph; else below the lowest raster
        float nodeX = 0.0f, nodeY = 0.0f;
    };
    NodeId importImage(const ImageFile& image, const ImportOptions& options);
    NodeId importTiles(const TileGrid& tiles, const std::string& name, const ImportOptions& options);
    // Deletes nodes and heals the graph around them: whatever a deleted node fed is re-fed by
    // the node upstream of it, found by walking up through deleted nodes that have exactly one
    // input (a unary input, or a one-layer stack). Nodes with several inputs or none (a
    // compositor, a raster) leave their users unconnected. The master is never deleted.
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

    // ---- brush strokes on the active raster (viewport). Dabs go into the Project's scratch
    // stroke and are recomposed over the raster's committed pixels into its draft every call:
    // the graph and the viewport update live, the committed pixels stay untouched. endStroke()
    // commits the draft (one commit in the raster's own history, one step in the project's).
    void beginStroke(const BrushParams& params, StrokePoint at);
    void strokeTo(std::span<const StrokePoint> path);   // through every point, applied once
    void endStroke();
    bool strokeActive() const { return m_stroke.active(); }

    // ---- history: one step per user action. Raster pixels live in each raster's own commits
    // (RasterData::history()); a step refers to the commits it made.
    bool canUndo() const { return !m_stroke.active() && m_history.canUndo(); }
    bool canRedo() const { return !m_stroke.active() && m_history.canRedo(); }
    bool undo();
    bool redo();
    size_t historyBytes() const;   // what the rasters keep for undo

    // ---- events (delivered synchronously, in subscription order)
    using Listener = std::function<void(const ProjectEvent&)>;
    int  subscribe(Listener l);
    void unsubscribe(int token);

private:
    NodeId adopt(NodePtr n, float x, float y);   // registers a new node, emits NodeAdded
    NodePtr bridgeSource(const Node& n, const absl::flat_hash_set<NodeId>& deleted) const;
    bool activeRasterEditable() const;
    RasterRootNode* rasterNode(RasterId id) const;
    void emit(ProjectEventType t, NodeId n = kNoNodeId);
    void applyStrokeChanges();
    RasterRootNode* strokeRaster() const;
    void flipRasterCommit(const RasterCommitRef& c);

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
    BrushStroke m_stroke;          // the scratch strokes are painted into
    NodeId m_strokeNode = kNoNodeId;   // the raster it paints into

    EditPreview m_preview;

    std::vector<std::pair<int, Listener>> m_listeners;
    int m_nextToken = 1;
};

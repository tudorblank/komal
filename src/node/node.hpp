#pragma once
// The node graph core: signals, masks, the per-tile render path and change propagation.
//
// Nodes produce tiles on demand (fetch), shaped by signals:
//   Main  the output. Every node has it.
//   Wet   effect nodes only (hasDryWet: Move, ...): the processed input.
//   Dry   effect nodes only: the input untouched (off by default). Main = Wet over Dry.
//
// Each signal is shaped per channel (A, R, G, B) by a 0..1 scale times an optional mask: another
// node's output as greyscale over white (transparent = untouched, opaque black = 0).
// Main.A is the opacity.
//
// Edits never recompute eagerly: they mark tiles dirty down to every listener (inputs and
// masks). Rendering pulls. Readers outside the graph (GPU copies of an output: the canvas, a
// thumbnail) follow a node's output tile by tile through a TileWatch.

#include "raster/raster-data.hpp"

#include <array>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

class Node;
using NodePtr = std::shared_ptr<Node>;
enum class NodeId : uint32_t {};   // assigned by the Project, never reused; its own type: never mixed up with another number
constexpr NodeId kNoNodeId{};

enum class NodeType    : uint8_t { Raster, Reference, Move, Compositor };
enum class NodeSignal  : uint8_t { Main, Dry, Wet };
enum class NodeChannel : uint8_t { A, R, G, B };
constexpr int kSignalCount  = 3;
constexpr int kChannelCount = 4;

// A kind's own settings, beyond its signals and links: one struct per kind that has any. The node
// owns them (Node::settings / setSettings); history snapshots them and the graph's controls show
// them without knowing node classes. A kind with new settings adds its struct here.
struct NoSettings {
    bool operator==(const NoSettings&) const = default;
};
struct RasterSettings {
    bool locked      = false;   // no pixel edits at all
    bool alphaLocked = false;   // edits keep every pixel's alpha
    bool operator==(const RasterSettings&) const = default;
};
struct MoveSettings {
    int x = 0, y = 0;           // the offset, whole pixels (TileUtils::inWorld)
    bool operator==(const MoveSettings&) const = default;
};
using NodeSettings = std::variant<NoSettings, RasterSettings, MoveSettings>;

struct SignalChannels {
    float   scale[kChannelCount] = { 1, 1, 1, 1 };   // A, R, G, B in 0..1
    NodePtr mask[kChannelCount]  = {};
    bool isIdentity() const
    {
        for (int i = 0; i < kChannelCount; ++i) if (scale[i] != 1.0f || mask[i]) return false;
        return true;
    }
};

// Per-tile channel gain: factor(i) = perPixel ? mul255(k, perPixel[i]) : k.
struct Gain {
    uint8_t        k = 255;
    const uint8_t* perPixel = nullptr;
    bool isUnit() const { return k == 255 && !perPixel; }
};

// Scratch memory for one top-level tile render. One per thread.
struct MaskTile { uint8_t v[Tile::kPixels]; };

class RenderCtx {
public:
    void beginTile() { m_used = 0; m_maskUsed = 0; m_masks.clear(); }   // before each top-level fetch

    Tile& take();                                        // a cleared scratch tile
    size_t mark() const { return m_used; }
    void rewind(size_t m) { assert(m <= m_used); m_used = m; }   // frees scratch taken after mark()

    MaskTile& newMask();
    // Masks computed during this render, per (mask node, tile). nullptr: 0 there;
    // kFullMask: 255 everywhere there (free, acts as no mask).
    bool findMask(const Node* n, int tx, int ty, const MaskTile*& out) const;
    void storeMask(const Node* n, int tx, int ty, const MaskTile* m) { m_masks[MaskKey{ n, TileUtils::packKey(tx, ty) }] = m; }
    static const MaskTile* const kFullMask;

private:
    struct MaskKey {
        const Node* node;
        TileKey     tile;
        bool operator==(const MaskKey&) const = default;
        template<class H> friend H AbslHashValue(H h, const MaskKey& k) { return H::combine(std::move(h), k.node, k.tile); }
    };
    std::vector<std::unique_ptr<Tile>>     m_tiles;
    size_t                                 m_used = 0;
    std::vector<std::unique_ptr<MaskTile>> m_maskPool;
    size_t                                 m_maskUsed = 0;
    absl::flat_hash_map<MaskKey, const MaskTile*> m_masks;
};

// A node's output changes, for one reader outside the graph: filled by the node, drained by the
// reader. Starts full: a new reader reads everything once.
class TileWatch {
public:
    // Drains it: true when everything must be read again (and `changed` is empty), else the tiles
    // whose output changed since the last take.
    bool take(TileSet& changed)
    {
        changed = std::exchange(m_changed, TileSet{});
        return std::exchange(m_full, false);
    }

private:
    friend class Node;
    static constexpr size_t kMaxTiles = 4096;   // past this, one full re-read is cheaper to track

    void add(const TileSet& tiles)
    {
        if (m_full) return;
        m_changed.insert(tiles.begin(), tiles.end());
        if (m_changed.size() > kMaxTiles) { m_changed.clear(); m_full = true; }
    }

    bool    m_full = true;
    TileSet m_changed;
};

enum class Bounds { Keep, Recompute };
enum class Evict  { Output, All };

// The right to change the graph: every call that edits a node takes one. Only the Project makes
// them, and lends one to the blocks it edits the graph through. Anyone may read a node; nobody
// else can change one.
class GraphKey {
    GraphKey() = default;
    friend class Project;
};

// ============================ NODE ============================
class Node : public std::enable_shared_from_this<Node> {
public:
    Node();
    virtual ~Node() = default;
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    // ---------- identity: the document names and places nodes
    virtual NodeType type() const = 0;
    NodeId id() const { return m_id; }
    const std::string& label() const { return m_label; }
    float x() const { return m_x; }   // position in the node editor
    float y() const { return m_y; }
    void setId(GraphKey, NodeId id) { m_id = id; }
    void setLabel(GraphKey, std::string label) { m_label = std::move(label); }
    void setPosition(GraphKey, float x, float y) { m_x = x; m_y = y; }

    // ---------- its kind's own settings (NodeSettings)
    virtual NodeSettings settings() const { return NoSettings{}; }
    virtual void setSettings(GraphKey, const NodeSettings&) {}   // its own kind's struct (validated by the caller)

    // ---------- what a subclass implements (raw signals; the base applies gains and masks)
    virtual bool hasDryWet() const { return false; }   // effect nodes
    virtual std::span<const NodePtr> inputs() const { return {}; }
    virtual BoundsI wetBounds() const = 0;
    virtual const Tile* fetchWet(int tx, int ty, RenderCtx& ctx) = 0;
    virtual void wetFootprint(TileSet& out) = 0;
    virtual void mapInputTiles(const TileSet& in, TileSet& out) const { out.insert(in.begin(), in.end()); }

    virtual BoundsI dryBounds() const { return {}; }   // default: dry adds nothing
    virtual const Tile* fetchDry(int, int, RenderCtx&) { return nullptr; }
    virtual void dryFootprint(TileSet&) {}

    // ---------- the one read path
    // Valid until ctx.beginTile() / rewind past it, or the next raster edit. Read-only.
    const Tile* fetch(int tx, int ty, RenderCtx& ctx);

    // ---------- bounds / footprint
    BoundsI bounds() const;   // cached until something changes them
    void addFootprint(TileSet& out) { if (wetOn()) wetFootprint(out); if (dryOn()) dryFootprint(out); }
    TileSet footprint() { TileSet t; addFootprint(t); return t; }

    // ---------- caches (subclasses that keep any)
    virtual void evictRawTile(int, int) {}      // caches derived from inputs
    virtual void evictOutputTile(int, int) {}   // caches of post-signal output

    // ---------- signals / masks
    const SignalChannels& signal(NodeSignal s) const { return m_signals[int(s)]; }
    bool hasSignal(NodeSignal s) const { return s == NodeSignal::Main || hasDryWet(); }
    bool wetOn() const { return !hasDryWet() || gainByte(m_signals[int(NodeSignal::Wet)].scale[0]) != 0; }
    bool dryOn() const { return hasDryWet() && gainByte(m_signals[int(NodeSignal::Dry)].scale[0]) != 0; }
    bool isPassthrough() const
    { return signal(NodeSignal::Main).isIdentity() && signal(NodeSignal::Wet).isIdentity() && !dryOn(); }

    void setScale(GraphKey, NodeSignal sig, NodeChannel ch, float v);         // ignored for a signal the node lacks
    bool linkMask(GraphKey, NodePtr mask, NodeSignal sig, NodeChannel ch);   // nullptr unlinks; false: cycle, or no such signal
    bool hasMasks() const;
    bool clearMasksTo(GraphKey, const NodePtr& n);   // drops every mask slot that points at n
    void unlinkAllMasks(GraphKey);

    // ---------- graph
    virtual void replaceInputInstance(GraphKey, const NodePtr&, const NodePtr&) {}
    virtual void detachAllInputs(GraphKey) {}
    std::vector<NodePtr> allDeps() const;   // inputs and masks
    bool usesAsInput(const Node* n) const;
    bool usesAsMask(const Node* n) const;
    bool dependsOn(const Node* t) const;
    bool canDependOn(const NodePtr& n) const { return !n || (n.get() != this && !n->dependsOn(this)); }
    uint64_t revision() const { return m_revision; }   // bumps whenever the output may have changed

    // A reader outside the graph: the node reports every output tile that changes into it, for as
    // long as the reader keeps it.
    std::shared_ptr<TileWatch> watchOutput();

    void removeFromGraph(GraphKey);   // when deleting: users drop it, it drops its inputs and masks

    static uint8_t gainByte(float v) { return uint8_t(std::clamp(int(v * 255.0f + 0.5f), 0, 255)); }

protected:
    // Unknown change: dirty = footprint before + after.
    TileSet change(const std::function<void()>& mutate, Evict e = Evict::All);
    // Known change: exactly `dirty`.
    TileSet changeKnown(TileSet dirty, Bounds b, Evict e, const std::function<void()>& mutate);

    void emitDirty(const TileSet& tiles, Evict e);
    void invalidateBounds();

private:
    // Dirty tiles from a dependency (raster edits enter the graph here).
    void dependencyDirty(const Node* from, const TileSet& tiles);

    NodeId      m_id = kNoNodeId;
    std::string m_label;
    float       m_x = 0, m_y = 0;
    std::array<SignalChannels, kSignalCount> m_signals;
    mutable bool    m_boundsDirty = true;
    mutable BoundsI m_bounds;
    uint64_t m_revision = 0;
    std::vector<std::weak_ptr<Node>> m_listeners;   // nodes that use this one (input or mask)
    std::vector<std::weak_ptr<TileWatch>> m_watches;   // readers outside the graph
    std::vector<NodePtr> m_wired;                   // deps this node is registered with

    void finishChange(const TileSet& d, Bounds b, Evict e);
    void rewire();
    bool dependsOnImpl(const Node* t, absl::flat_hash_set<const Node*>& seen) const;

    // signal pipeline
    const MaskTile* maskTile(Node& mask, int tx, int ty, RenderCtx& ctx);
    bool planGains(const SignalChannels& s, int tx, int ty, RenderCtx& ctx, Gain (&g)[4]);
    const Tile* applyGains(const Tile* raw, const Gain (&g)[4], RenderCtx& ctx);
    const Tile* renderSignal(NodeSignal sig, int tx, int ty, RenderCtx& ctx);
    const Tile* blendOver(const Tile* top, const Tile* bottom, RenderCtx& ctx);
};

// The top-level fetch: resets the context, then renders tile (tx, ty) of `n`.
inline const Tile* renderTile(Node& n, int tx, int ty, RenderCtx& ctx) { ctx.beginTile(); return n.fetch(tx, ty, ctx); }

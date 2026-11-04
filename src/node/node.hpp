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
// What feeds a node is its links (NodeLinks), landing on its ports: its kind declares them (Ports:
// nothing, one input or a stack feeding its wet, and a mask per channel of each signal it has).
// Links change in one place, Node::link / unlink / moveLayer / setLinks, which knows each port's
// rules: what may land there, and which tiles change when it does.
//
// Edits never recompute eagerly: they mark tiles dirty down to every listener (inputs and
// masks), each listener once per change, in an order where a node comes after everything it
// reads (Kahn's algorithm over what's downstream, kept until a link below it changes). Rendering
// pulls. Readers outside the graph (GPU copies of an output: the canvas, a thumbnail) follow a
// node's output tile by tile through a TileWatch.

#include "node/input-slot.hpp"
#include "node/signals.hpp"
#include "node/stack-groups.hpp"
#include "raster/raster-data.hpp"

#include <algorithm>
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
enum class NodeId : uint32_t {};   // assigned by the Workspace, never reused; its own type: never mixed up with another number
constexpr NodeId kNoNodeId{};

enum class NodeType : uint8_t { Raster, Reference, Move, Compositor };

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
struct CompositorSettings {
    std::vector<NodeId>     muted;    // its layers left out of the composite, sorted (mutes())
    std::vector<StackGroup> groups;   // in stack order, none overlapping (StackGroups::normalized); kept right as rows come, go and move
    bool mutes(NodeId layer) const { return std::binary_search(muted.begin(), muted.end(), layer); }
    bool operator==(const CompositorSettings&) const = default;
};
using NodeSettings = std::variant<NoSettings, RasterSettings, MoveSettings, CompositorSettings>;

// What a kind's links can land on (its class declares them: kPorts). Every signal it has takes a
// mask per channel besides.
enum class InputShape : uint8_t {
    None,    // nothing feeds its wet: it makes its own pixels (a raster)
    One,     // one input
    Stack,   // a stack of layers, bottom first, each node at most once
};
struct Ports {
    InputShape inputs = InputShape::None;
    bool       dryWet = false;   // an effect: Dry and Wet signals besides Main
};

// Everything that feeds a node: its input (InputShape::One), its layers, bottom first (Stack), and
// its masks (null: none). Only the parts its ports have are ever set.
struct NodeLinks {
    NodePtr input;
    std::vector<NodePtr> layers;
    NodePtr masks[kSignalCount][kChannelCount] = {};

    // Every link with the slot it lands on: the input, the layers bottom first, then the masks.
    std::vector<std::pair<NodePtr, InputSlot>> all() const;
    bool empty() const;   // nothing feeds it
    bool operator==(const NodeLinks&) const = default;
};

struct SignalChannels {
    float scale[kChannelCount] = { 1, 1, 1, 1 };   // A, R, G, B in 0..1
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

// The right to change the graph: every call that edits a node takes one. Only the Workspace makes
// them, and lends one to the blocks it edits the graph through. Anyone may read a node; nobody
// else can change one.
class GraphKey {
    GraphKey() = default;
    friend class Workspace;
};

// ============================ NODE ============================
class Node : public std::enable_shared_from_this<Node> {
public:
    explicit Node(Ports ports);   // its kind's (its class's kPorts)
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

    // ---------- signals
    const SignalChannels& signal(NodeSignal s) const { return m_signals[int(s)]; }
    bool hasDryWet() const { return m_ports.dryWet; }   // an effect
    bool hasSignal(NodeSignal s) const { return s == NodeSignal::Main || hasDryWet(); }
    bool wetOn() const { return !hasDryWet() || gainByte(m_signals[int(NodeSignal::Wet)].scale[0]) != 0; }
    bool dryOn() const { return hasDryWet() && gainByte(m_signals[int(NodeSignal::Dry)].scale[0]) != 0; }
    bool isPassthrough() const { return signalIsIdentity(NodeSignal::Main) && signalIsIdentity(NodeSignal::Wet) && !dryOn(); }
    void setScale(GraphKey, NodeSignal sig, NodeChannel ch, float v);   // ignored for a signal the node lacks

    // ---------- links: what feeds it (NodeLinks), on its ports
    const Ports&     ports() const { return m_ports; }
    const NodeLinks& links() const { return m_links; }
    std::span<const NodePtr> inputs() const;           // what feeds its wet: its input, or its layers
    const NodePtr& mask(NodeSignal s, NodeChannel c) const { return m_links.masks[int(s)][int(c)]; }
    NodePtr linkAt(const InputSlot& slot) const;        // what feeds that slot (a new layer is a position: nothing)
    // Whether `source` may land on `slot`: a port it has, never a cycle, a node once per stack (on
    // its own row again is fine; at a new position in its own stack it moves there).
    bool canLink(const NodePtr& source, const InputSlot& slot) const;
    void link(GraphKey, const NodePtr& source, const InputSlot& slot);   // where canLink() said yes
    void unlink(GraphKey, const InputSlot& slot);                         // nothing there: nothing happens
    void moveLayer(GraphKey, size_t from, size_t to);                     // in its stack, both in range
    void unlinkSource(GraphKey, const Node& source);   // every link from `source` (it's going)
    // Exactly `want` (what its ports take; a link that would make a cycle is left out). Everything
    // it held goes first: a whole graph restored node by node never passes through a cycle.
    void setLinks(GraphKey, const NodeLinks& want);

    std::vector<NodePtr> allDeps() const;   // inputs and masks, each once
    // Everything that reads it, directly or not, each after everything it reads (cached).
    std::vector<NodePtr> downstream();
    bool usesAsInput(const Node* n) const;
    bool usesAsMask(const Node* n) const;
    bool dependsOn(const Node* t) const;
    bool canDependOn(const NodePtr& n) const { return !n || (n.get() != this && !n->dependsOn(this)); }

    // A reader outside the graph: the node reports every output tile that changes into it, for as
    // long as the reader keeps it.
    std::shared_ptr<TileWatch> watchOutput();

    void removeFromGraph(GraphKey);   // when deleting: users drop it, it drops what fed it

    static uint8_t gainByte(float v) { return unitToByte(v); }

protected:
    // A stack's rows as they change (inside the edit, before what changed is told): what keeps
    // something per row, a compositor's mutes and groups, follows them.
    struct LayerInserted { size_t at; };
    struct LayerRemoved  { size_t at; };
    struct LayerReplaced { size_t at; };   // another node on the same row
    struct LayerMoved    { size_t from, to; };
    struct LayersCleared {};
    using LayerEdit = std::variant<LayerInserted, LayerRemoved, LayerReplaced, LayerMoved, LayersCleared>;
    virtual void layersEdited(const LayerEdit&) {}
    // Tiles a layer moving from row `from` to `to` changes besides where it crosses the others.
    virtual void movedLayerTiles(size_t /*from*/, size_t /*to*/, const TileSet& /*moved*/, TileSet& /*dirty*/) const {}

    // Unknown change: dirty = footprint before + after.
    TileSet change(const std::function<void()>& mutate, Evict e = Evict::All);
    // Known change: exactly `dirty`.
    TileSet changeKnown(TileSet dirty, Bounds b, Evict e, const std::function<void()>& mutate);

    // Tiles of its output changed (raster edits enter the graph here): it, then everything
    // downstream, each once.
    void emitDirty(const TileSet& tiles, Evict e);
    void invalidateBounds();

private:
    // What's downstream of it (through listeners), each after everything it reads: Kahn's
    // algorithm over that part of the graph. Kept until a listener list at or below it changes:
    // then it, and everything upstream of the node whose list changed, drop theirs.
    struct Downstream {
        std::vector<std::weak_ptr<Node>> order;
        absl::flat_hash_map<const Node*, uint32_t> index;   // into order
    };
    // Dirty tiles a node takes from the ones it reads, before it passes them on.
    struct Incoming {
        TileSet tiles;
        bool    raw = false;   // from an input: caches derived from inputs go too
    };
    const Downstream& downstreamOrder();
    void dropOrders();   // this node's and every node's it reads, directly or not
    void takeDirty(const TileSet& tiles, Evict e);   // its own part: readers told, caches evicted
    void receive(const Node& from, const TileSet& tiles, Incoming& in) const;   // from a dependency
    bool signalIsIdentity(NodeSignal s) const;

    const Ports m_ports;
    NodeId      m_id = kNoNodeId;
    std::string m_label;
    float       m_x = 0, m_y = 0;
    std::array<SignalChannels, kSignalCount> m_signals;
    NodeLinks   m_links;
    mutable bool    m_boundsDirty = true;
    mutable BoundsI m_bounds;
    std::vector<std::weak_ptr<Node>> m_listeners;   // nodes that use this one (input or mask)
    std::vector<std::weak_ptr<TileWatch>> m_watches;   // readers outside the graph
    std::vector<NodePtr> m_wired;                   // deps this node is registered with
    std::unique_ptr<Downstream> m_downstream;       // made on first use; null: worked out again when asked

    void finishChange(const TileSet& d, Bounds b, Evict e);
    void rewire();
    bool dependsOnImpl(const Node* t, absl::flat_hash_set<const Node*>& seen) const;

    // signal pipeline
    const MaskTile* maskTile(Node& mask, int tx, int ty, RenderCtx& ctx);
    bool planGains(NodeSignal sig, int tx, int ty, RenderCtx& ctx, Gain (&g)[4]);
    const Tile* applyGains(const Tile* raw, const Gain (&g)[4], RenderCtx& ctx);
    const Tile* renderSignal(NodeSignal sig, int tx, int ty, RenderCtx& ctx);
    const Tile* blendOver(const Tile* top, const Tile* bottom, RenderCtx& ctx);
};

// The top-level fetch: resets the context, then renders tile (tx, ty) of `n`.
inline const Tile* renderTile(Node& n, int tx, int ty, RenderCtx& ctx) { ctx.beginTile(); return n.fetch(tx, ty, ctx); }

// A stack's own settings: what a node whose inputs are a stack (ports().inputs == InputShape::Stack)
// holds, its mutes and its groups; any other node's: none. The one way to read a stack from outside
// node/ (by its shape and settings, never by casting to its class); it changes through setSettings.
CompositorSettings stackSettings(const Node& n);

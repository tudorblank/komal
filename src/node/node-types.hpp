#pragma once
// The concrete nodes: the raster root (the only place pixels are edited), single-input nodes
// (Reference, Move) and the compositor (an ordered stack; the master is one).

#include "node/node.hpp"

// ============================ RASTER ROOT ============================
class RasterRootNode : public Node {
    struct Private { explicit Private() = default; };
public:
    static constexpr Ports kPorts{ InputShape::None, false };   // makes its own pixels
    RasterRootNode(Private, RasterData& r) : Node(kPorts), m_raster(&r) {}
    static std::shared_ptr<RasterRootNode> create(RasterData& r);
    NodeType type() const override { return NodeType::Raster; }

    const RasterData& raster() const { return *m_raster; }
    // Editing policy only (the Project and the brush enforce it): RasterSettings.
    bool locked() const { return m_settings.locked; }
    bool alphaLocked() const { return m_settings.alphaLocked; }
    NodeSettings settings() const override { return m_settings; }
    void setSettings(GraphKey, const NodeSettings& s) override { if (const auto* r = std::get_if<RasterSettings>(&s)) m_settings = *r; }

    // The only ways to change its pixels; each tells the graph what changed, in the graph or not
    // (a deleted raster flipped by a history jump comes back with its caches right).
    // An edit, recorded as one commit in the raster's history (kNoRasterCommit: nothing changed):
    RasterCommitId recordEdit(GraphKey, const std::function<void(RasterEditor&)>& body);
    void flip(GraphKey, RasterCommitId commit);   // undo / redo of one of its commits

    BoundsI wetBounds() const override { return m_raster->getBounds(); }
    const Tile* fetchWet(int tx, int ty, RenderCtx&) override { return m_raster->getReadTile(tx, ty); }   // zero-copy
    void wetFootprint(TileSet& out) override;

private:
    RasterData* m_raster;   // owned by the Project, outlives this node
    RasterSettings m_settings;
    TileSet  m_fp;          // footprint cache
    uint64_t m_fpRev = ~0ull;
    void publish(const EditSummary& s);
};

// ============================ ONE INPUT ============================
// What reads one input (InputShape::One): its Dry is that input, untouched.
class UnaryNode : public Node {
public:
    const NodePtr& input() const { return links().input; }

protected:
    explicit UnaryNode(Ports ports) : Node(ports) {}
    BoundsI dryBounds() const override { return input() ? input()->bounds() : BoundsI{}; }
    const Tile* fetchDry(int tx, int ty, RenderCtx& ctx) override { return input() ? input()->fetch(tx, ty, ctx) : nullptr; }
    void dryFootprint(TileSet& out) override { if (input()) input()->addFootprint(out); }
};

// A second, independently shaped view of another node's output.
class ReferenceNode : public UnaryNode {
    struct Private { explicit Private() = default; };
public:
    static constexpr Ports kPorts{ InputShape::One, false };
    explicit ReferenceNode(Private) : UnaryNode(kPorts) {}
    static std::shared_ptr<ReferenceNode> create();
    NodeType type() const override { return NodeType::Reference; }

    BoundsI wetBounds() const override { return input() ? input()->bounds() : BoundsI{}; }
    void wetFootprint(TileSet& out) override { if (input()) input()->addFootprint(out); }
    const Tile* fetchWet(int tx, int ty, RenderCtx& ctx) override { return input() ? input()->fetch(tx, ty, ctx) : nullptr; }
};

// Shifts its input by whole pixels. Wet: the moved input; Dry: the input where it was.
class MoveNode : public UnaryNode {
    struct Private { explicit Private() = default; };
public:
    static constexpr Ports kPorts{ InputShape::One, true };
    explicit MoveNode(Private) : UnaryNode(kPorts) {}
    static std::shared_ptr<MoveNode> create();
    NodeType type() const override { return NodeType::Move; }

    int offsetX() const { return m_x; }
    int offsetY() const { return m_y; }
    TileSet setOffset(GraphKey, int x, int y);
    NodeSettings settings() const override { return MoveSettings{ m_x, m_y }; }
    void setSettings(GraphKey key, const NodeSettings& s) override { if (const auto* m = std::get_if<MoveSettings>(&s)) setOffset(key, m->x, m->y); }

    BoundsI wetBounds() const override { return input() ? input()->bounds().offsetBy(m_x, m_y) : BoundsI{}; }
    void wetFootprint(TileSet& out) override;
    void mapInputTiles(const TileSet& in, TileSet& out) const override;
    const Tile* fetchWet(int tx, int ty, RenderCtx& ctx) override;

private:
    int m_x = 0, m_y = 0;
};

// ============================ COMPOSITOR ============================
// How a group of a stack shows (the document's: an artboard's rect and colour, and its eye).
struct GroupLook {
    GroupKey key = kNoGroup;        // the StackGroup's
    BoundsI  fill;                  // filled with `color` behind its layers (empty: nothing)
    Color8   color{ 0, 0, 0, 0 };   // straight; transparent: no fill
    bool     hidden = false;        // its fill and its layers left out
    bool operator==(const GroupLook&) const = default;
};

// layers()[0] is the bottom of the stack. Each layer may be muted (CompositorSettings), and runs
// of layers may be grouped, a group drawing a fill behind its layers (GroupLook).
class CompositorNode : public Node {
    struct Private { explicit Private() = default; };
public:
    static constexpr Ports kPorts{ InputShape::Stack, false };
    explicit CompositorNode(Private) : Node(kPorts) {}
    static std::shared_ptr<CompositorNode> create();
    NodeType type() const override { return NodeType::Compositor; }

    const std::vector<NodePtr>& layers() const { return links().layers; }
    NodeSettings settings() const override { return m_settings; }
    void setSettings(GraphKey, const NodeSettings& s) override;   // kept to its rows: mutes of its layers, groups in range
    const CompositorSettings& stack() const { return m_settings; }
    bool muted(const Node& layer) const;
    bool shows(size_t row) const { return row < m_shown.size() && m_shown[row]; }   // neither muted nor in a hidden group
    void setLooks(GraphKey, std::vector<GroupLook> looks);   // by group key; a group without one draws nothing of its own
    const std::vector<GroupLook>& looks() const { return m_looks; }

    // What the composite draws, bottom first: a layer shown, or a group's fill.
    struct Drawn {
        int      row = -1;    // the layer's; -1: a fill
        GroupKey key = kNoGroup;   // a fill's: its group ...
        uint32_t at  = 0;          // ... and the row it's under
        BoundsI  fill;
        RGBA     color{};     // premultiplied ...
        Color8   tint{};      // ... and straight
    };
    const std::vector<Drawn>& drawn() const { return m_drawn; }

    BoundsI wetBounds() const override;
    void wetFootprint(TileSet& out) override;
    const Tile* fetchWet(int tx, int ty, RenderCtx& ctx) override;

    // Cache of the raw composite (the master keeps one: the canvas reads it tile by tile).
    void enableCache(GraphKey, bool on);
    bool cacheEnabled() const { return m_cacheOn; }

    void evictRawTile(int tx, int ty) override;

protected:
    void layersEdited(const LayerEdit& e) override;
    void movedLayerTiles(size_t from, size_t to, const TileSet& moved, TileSet& dirty) const override;

private:
    const Tile* composeTile(int tx, int ty, RenderCtx& ctx);
    void refresh();   // m_shown and m_drawn, from the rows, the settings and the looks
    // Changes it through `mutate` (settings or looks): dirty where what it draws differs.
    void changeDrawing(const std::function<void()>& mutate);

    CompositorSettings     m_settings;
    std::vector<GroupLook> m_looks;
    std::vector<char>      m_shown;   // per row
    std::vector<Drawn>     m_drawn;
    bool     m_cacheOn  = false;
    TileGrid m_cache;       // raw composite
    TileSet  m_empty;       // tiles known to composite to nothing
};

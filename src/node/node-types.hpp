#pragma once
// The concrete nodes: the raster root (the only place pixels are edited), single-input nodes
// (Reference, Move) and the compositor (an ordered stack; the master is one).

#include "node/node.hpp"

// ============================ RASTER ROOT ============================
class RasterRootNode : public Node {
    struct Private { explicit Private() = default; };
public:
    RasterRootNode(Private, RasterData& r) : m_raster(&r) {}
    static std::shared_ptr<RasterRootNode> create(RasterData& r);

    const RasterData& raster() const { return *m_raster; }
    // Editing policy only (the Project and the brush enforce it).
    bool locked() const { return m_locked; }                 // no pixel edits at all
    void setLocked(bool locked) { m_locked = locked; }
    bool alphaLocked() const { return m_alphaLocked; }       // edits keep every pixel's alpha
    void setAlphaLocked(bool locked) { m_alphaLocked = locked; }

    // The only ways to change its pixels; each tells the graph what changed.
    // Straight into the committed pixels, not recorded (imports, starter content):
    EditSummary edit(const std::function<void(RasterEditor&)>& body);
    // An edit in progress, into the draft (shown live, committed later); `body` returns the tiles it changed:
    void draft(const std::function<TileSet(RasterData&)>& body);
    RasterCommitId commitDraft();   // one commit in the raster's history (kNoRasterCommit: nothing changed)
    void discardDraft();
    void flip(RasterCommitId commit);   // undo / redo of one of its commits

    BoundsI wetBounds() override { return m_raster->getBounds(); }
    const Tile* fetchWet(int tx, int ty, RenderCtx&) override { return m_raster->getReadTile(tx, ty); }   // zero-copy
    void wetFootprint(TileSet& out) override;

private:
    RasterData* m_raster;   // owned by the Project, outlives this node
    bool     m_locked = false;
    bool     m_alphaLocked = false;
    TileSet  m_fp;          // footprint cache
    uint64_t m_fpRev = ~0ull;
    void publish(const EditSummary& s);
};

// ============================ ONE INPUT ============================
class UnaryNode : public Node {
public:
    const NodePtr& input() const { return m_input; }
    bool setInput(NodePtr in);   // false if it would make a cycle

    std::span<const NodePtr> inputs() const override { return { &m_input, m_input ? 1u : 0u }; }
    void replaceInputInstance(const NodePtr& o, const NodePtr& n) override { if (m_input == o) setInput(n); }
    void detachAllInputs() override { setInput(nullptr); }

protected:
    NodePtr m_input;
    // Dry = the input, untouched (for the effect nodes among the subclasses)
    BoundsI dryBounds() override { return m_input ? m_input->bounds() : BoundsI{}; }
    const Tile* fetchDry(int tx, int ty, RenderCtx& ctx) override { return m_input ? m_input->fetch(tx, ty, ctx) : nullptr; }
    void dryFootprint(TileSet& out) override { if (m_input) m_input->addFootprint(out); }
};

// A second, independently shaped view of another node's output.
class ReferenceNode : public UnaryNode {
    struct Private { explicit Private() = default; };
public:
    explicit ReferenceNode(Private) {}
    static std::shared_ptr<ReferenceNode> create(NodePtr target = nullptr);

    BoundsI wetBounds() override { return m_input ? m_input->bounds() : BoundsI{}; }
    void wetFootprint(TileSet& out) override { if (m_input) m_input->addFootprint(out); }
    const Tile* fetchWet(int tx, int ty, RenderCtx& ctx) override { return m_input ? m_input->fetch(tx, ty, ctx) : nullptr; }
};

// Shifts its input by whole pixels. Wet: the moved input; Dry: the input where it was.
class MoveNode : public UnaryNode {
    struct Private { explicit Private() = default; };
public:
    explicit MoveNode(Private) {}
    static std::shared_ptr<MoveNode> create();
    bool hasDryWet() const override { return true; }

    int offsetX() const { return m_x; }   // applied: what the CPU pixels use
    int offsetY() const { return m_y; }
    TileSet setOffset(int x, int y);

    // While a drag previews an offset, it is what the node shows: its bounds (and everything
    // downstream's), the GPU preview and the graph all read shownX/Y. The CPU pixels stay at the
    // applied offset until it's applied.
    void previewOffset(int x, int y);
    void endPreview();   // back to the applied offset; tiles read meanwhile are re-read
    int  shownX() const { return m_previewing ? m_px : m_x; }
    int  shownY() const { return m_previewing ? m_py : m_y; }

    BoundsI wetBounds() override { return m_input ? m_input->bounds().offsetBy(shownX(), shownY()) : BoundsI{}; }
    void wetFootprint(TileSet& out) override;
    void mapInputTiles(const TileSet& in, TileSet& out) const override;
    const Tile* fetchWet(int tx, int ty, RenderCtx& ctx) override;

private:
    int  m_x = 0, m_y = 0;
    int  m_px = 0, m_py = 0;   // the previewed offset
    bool m_previewing = false;
};

// ============================ COMPOSITOR ============================
// layers()[0] is the bottom of the stack.
class CompositorNode : public Node {
    struct Private { explicit Private() = default; };
public:
    explicit CompositorNode(Private) {}
    static std::shared_ptr<CompositorNode> create();

    const std::vector<NodePtr>& layers() const { return m_layers; }
    std::span<const NodePtr> inputs() const override { return m_layers; }
    bool has(const NodePtr& n) const { return std::find(m_layers.begin(), m_layers.end(), n) != m_layers.end(); }

    bool addLayerAt(NodePtr in, size_t index);
    bool addLayer(NodePtr in) { return addLayerAt(std::move(in), m_layers.size()); }
    void removeLayer(size_t i);
    TileSet moveLayer(size_t from, size_t to);
    void replaceInputInstance(const NodePtr& o, const NodePtr& n) override;
    void detachAllInputs() override;

    BoundsI wetBounds() override;
    void wetFootprint(TileSet& out) override { for (auto& l : m_layers) l->addFootprint(out); }
    const Tile* fetchWet(int tx, int ty, RenderCtx& ctx) override;

    // Cache of the raw composite (the master keeps one: the canvas reads it tile by tile).
    void enableCache(bool on);
    bool cacheEnabled() const { return m_cacheOn; }

    void evictRawTile(int tx, int ty) override;

private:
    const Tile* composeTile(int tx, int ty, RenderCtx& ctx);

    std::vector<NodePtr> m_layers;
    bool     m_cacheOn  = false;
    TileGrid m_cache;       // raw composite
    TileSet  m_empty;       // tiles known to composite to nothing
};

// Layers [bottom, top) composited (Over), as a compositor does: the bottom first. The result
// lives in ctx scratch (nullptr: nothing there).
const Tile* composeStack(const NodePtr* bottom, const NodePtr* top, int tx, int ty, RenderCtx& ctx);

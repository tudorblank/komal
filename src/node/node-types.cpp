#include "node-types.hpp"

#include <algorithm>
#include <utility>

// ============================ RASTER ROOT ============================
std::shared_ptr<RasterRootNode> RasterRootNode::create(RasterData& r)
{
    auto n = std::make_shared<RasterRootNode>(Private{}, r);
    n->m_meta.type  = NodeType::Raster;
    n->m_meta.label = "Raster";
    return n;
}

EditSummary RasterRootNode::edit(const std::function<void(RasterEditor&)>& body)
{
    RasterEditor ed(*m_raster);
    try {
        body(ed);
    } catch (...) {
        publish(ed.commit());   // the editor finalizes, and the graph is still told
        throw;
    }
    EditSummary s = ed.commit();
    publish(s);
    return s;
}

void RasterRootNode::draft(const std::function<TileSet(RasterData&)>& body)
{
    const BoundsI before = m_raster->getBounds();
    const TileSet changed = body(*m_raster);
    if (m_raster->getBounds() != before) invalidateBounds();
    emitDirty(changed, Evict::All);
}

RasterCommitId RasterRootNode::commitDraft()
{
    // What the raster shows doesn't change (it already showed the draft): only its bounds can,
    // where the draft erased down to nothing.
    const BoundsI before = m_raster->getBounds();
    const RasterCommitId commit = m_raster->commitDraft();
    if (m_raster->getBounds() != before) invalidateBounds();
    return commit;
}

void RasterRootNode::discardDraft()
{
    const BoundsI before = m_raster->getBounds();
    const TileSet covered = m_raster->discardDraft();
    if (m_raster->getBounds() != before) invalidateBounds();
    emitDirty(covered, Evict::All);
}

void RasterRootNode::flip(RasterCommitId commit)
{
    publish(m_raster->flip(commit));
}

void RasterRootNode::wetFootprint(TileSet& out)
{
    if (m_fpRev != m_raster->revision()) {   // the committed tiles, cached
        m_fp.clear();
        m_raster->forEachTile([&](int tx, int ty, const Tile&) { m_fp.insert(TileUtils::packKey(tx, ty)); });
        m_fpRev = m_raster->revision();
    }
    out.insert(m_fp.begin(), m_fp.end());
    if (m_raster->hasDraft()) m_raster->forEachDraftTile([&](int tx, int ty) { out.insert(TileUtils::packKey(tx, ty)); });
}

void RasterRootNode::publish(const EditSummary& s)
{
    if (s.touched.empty()) return;
    if (s.boundsChanged) invalidateBounds();
    emitDirty(s.touched, Evict::All);
}

// ============================ ONE INPUT ============================
bool UnaryNode::setInput(NodePtr in)
{
    if (in == m_input) return true;
    if (!canDependOn(in)) return false;
    change([&] { m_input = std::move(in); });
    return true;
}

std::shared_ptr<ReferenceNode> ReferenceNode::create(NodePtr target)
{
    auto n = std::make_shared<ReferenceNode>(Private{});
    n->m_meta.type  = NodeType::Reference;
    n->m_meta.label = "Reference";
    if (target) n->setInput(std::move(target));
    return n;
}

std::shared_ptr<MoveNode> MoveNode::create()
{
    auto n = std::make_shared<MoveNode>(Private{});
    n->m_meta.type  = NodeType::Move;
    n->m_meta.label = "Move";
    return n;
}

TileSet MoveNode::setOffset(int x, int y)
{
    assert(TileUtils::inWorld(x) && TileUtils::inWorld(y));
    if (x == m_x && y == m_y) return {};
    return change([&] { m_x = x; m_y = y; });
}

void MoveNode::previewOffset(int x, int y)
{
    if (m_previewing && x == m_px && y == m_py) return;
    m_previewing = true;
    m_px = x;
    m_py = y;
    invalidateBounds();
}

void MoveNode::endPreview()
{
    if (!m_previewing) return;
    m_previewing = false;
    invalidateBounds();
    emitDirty(footprint(), Evict::All);   // reads during the drag were culled by the previewed bounds
}

void MoveNode::wetFootprint(TileSet& out)
{
    if (!m_input) return;
    TileSet raw;
    m_input->addFootprint(raw);
    mapInputTiles(raw, out);
}

void MoveNode::mapInputTiles(const TileSet& in, TileSet& out) const
{
    for (TileKey k : in) {
        const auto t = TileUtils::unpackKey(k);
        const int x0 = TileUtils::tileToWorld(t.x) + m_x, y0 = TileUtils::tileToWorld(t.y) + m_y;
        forEachTileSpan(BoundsI{ x0, y0, x0 + Tile::kSize - 1, y0 + Tile::kSize - 1 },
                        [&](const TileSpan& s) { out.insert(TileUtils::packKey(s.tx, s.ty)); });
    }
}

const Tile* MoveNode::fetchWet(int tx, int ty, RenderCtx& ctx)
{
    if (!m_input) return nullptr;
    if (((m_x | m_y) & TileUtils::kTileMask) == 0)   // whole-tile shift: forward, zero-copy
        return m_input->fetch(tx - (m_x >> TileUtils::kTileShift), ty - (m_y >> TileUtils::kTileShift), ctx);

    Tile& out = ctx.take();   // sub-tile shift: up to 4 source tiles
    const TileSpan d = TileSpan::wholeTile(tx, ty);
    const size_t mk = ctx.mark();
    bool any = false;
    {
        Tile::BulkWrite w(out, Tile::BulkWrite::Trusted);
        forEachTileSpan(d.worldBounds().offsetBy(-m_x, -m_y), [&](const TileSpan& s) {
            if (const Tile* st = m_input->fetch(s.tx, s.ty, ctx)) {
                copySpanShifted(w.data(), *st, s, d, m_x, m_y);
                w.markChanged();
                any = true;
            }
            ctx.rewind(mk);
        });
    }
    return any ? &out : nullptr;
}

// ============================ COMPOSITOR ============================
std::shared_ptr<CompositorNode> CompositorNode::create()
{
    auto n = std::make_shared<CompositorNode>(Private{});
    n->m_meta.type  = NodeType::Compositor;
    n->m_meta.label = "Compositor";
    return n;
}

bool CompositorNode::addLayerAt(NodePtr in, size_t index)
{
    if (!in || !canDependOn(in) || has(in)) return false;
    TileSet dirty;
    in->addFootprint(dirty);
    changeKnown(dirty, Bounds::Recompute, Evict::All,
                [&] { m_layers.insert(m_layers.begin() + std::min(index, m_layers.size()), std::move(in)); });
    return true;
}

void CompositorNode::removeLayer(size_t i)
{
    if (i >= m_layers.size()) return;
    TileSet dirty;
    m_layers[i]->addFootprint(dirty);
    changeKnown(dirty, Bounds::Recompute, Evict::All, [&] { m_layers.erase(m_layers.begin() + i); });
}

TileSet CompositorNode::moveLayer(size_t from, size_t to)
{
    if (from >= m_layers.size() || to >= m_layers.size() || from == to) return {};
    // Only tiles where the moved layer overlaps the layers it crosses can change.
    TileSet moved, others;
    m_layers[from]->addFootprint(moved);
    for (size_t i = std::min(from, to); i <= std::max(from, to); ++i)
        if (i != from) m_layers[i]->addFootprint(others);
    TileSet dirty;
    for (TileKey k : moved) if (others.contains(k)) dirty.insert(k);
    return changeKnown(dirty, Bounds::Keep, Evict::All, [&] {
        NodePtr n = std::move(m_layers[from]);
        m_layers.erase(m_layers.begin() + from);
        m_layers.insert(m_layers.begin() + to, std::move(n));
    });
}

void CompositorNode::replaceInputInstance(const NodePtr& o, const NodePtr& n)
{
    auto it = std::find(m_layers.begin(), m_layers.end(), o);
    if (o == n || it == m_layers.end() || !canDependOn(n)) return;
    TileSet dirty;
    o->addFootprint(dirty);
    if (n) n->addFootprint(dirty);
    const bool dup = n && has(n);
    changeKnown(dirty, Bounds::Recompute, Evict::All, [&] { if (n && !dup) *it = n; else m_layers.erase(it); });
}

void CompositorNode::detachAllInputs()
{
    if (m_layers.empty()) return;
    TileSet dirty;
    for (auto& l : m_layers) l->addFootprint(dirty);
    changeKnown(dirty, Bounds::Recompute, Evict::All, [&] { m_layers.clear(); });
}

BoundsI CompositorNode::wetBounds()
{
    BoundsI r;
    for (auto& l : m_layers) r.expand(l->bounds());
    return r;
}

void CompositorNode::enableCache(bool on)
{
    if (on == m_cacheOn) return;
    m_cacheOn = on;
    m_cache.clear();
    m_empty.clear();
}

const Tile* CompositorNode::fetchWet(int tx, int ty, RenderCtx& ctx)
{
    if (!m_cacheOn) return composeTile(tx, ty, ctx);

    const TileKey k = TileUtils::packKey(tx, ty);
    if (m_empty.contains(k)) return nullptr;                          // negative cache
    if (const Tile* hit = m_cache.getReadTile(tx, ty)) return hit;

    const size_t mk = ctx.mark();
    const Tile* built = composeTile(tx, ty, ctx);
    if (!built) { ctx.rewind(mk); m_empty.insert(k); return nullptr; }

    Tile& slot = m_cache.writeOrCreateTile(tx, ty);
    slot.copyFrom(*built);
    slot.settle();   // later const reads don't write
    ctx.rewind(mk);
    if (slot.isEmpty()) { m_cache.freeTile(tx, ty); m_empty.insert(k); return nullptr; }
    return &slot;
}

void CompositorNode::evictRawTile(int tx, int ty)
{
    if (!m_cacheOn) return;
    m_cache.freeTile(tx, ty);
    m_empty.erase(TileUtils::packKey(tx, ty));
}

const Tile* CompositorNode::composeTile(int tx, int ty, RenderCtx& ctx)
{
    return composeStack(m_layers.data(), m_layers.data() + m_layers.size(), tx, ty, ctx);
}

const Tile* composeStack(const NodePtr* bottom, const NodePtr* top, int tx, int ty, RenderCtx& ctx)
{
    // Top layer down, accumulating *under* what's there; stops once every pixel is opaque.
    Tile& acc = ctx.take();
    const size_t mk = ctx.mark();
    int  opaque = 0;
    bool any = false;
    {
        Tile::BulkWrite w(acc, Tile::BulkWrite::Trusted);
        RGBA* d = w.data();
        for (const NodePtr* it = top; it != bottom && opaque < Tile::kPixels;) {
            --it;
            if (const Tile* src = (*it)->fetch(tx, ty, ctx)) {
                const RGBA* s = src->getReadAll();
                if (!any) {
                    std::memcpy(d, s, sizeof(RGBA) * Tile::kPixels);
                    for (int i = 0; i < Tile::kPixels; ++i) opaque += d[i].a == 255;
                } else {
                    for (int i = 0; i < Tile::kPixels; ++i)
                        if (d[i].a != 255 && s[i].a) {
                            d[i] = over(s[i], d[i]);   // what's accumulated sits on top of this layer
                            opaque += d[i].a == 255;
                        }
                }
                any = true;
            }
            ctx.rewind(mk);
        }
        if (any) w.markChanged();
    }
    return any ? &acc : nullptr;
}

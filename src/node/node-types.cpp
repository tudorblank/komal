#include "node-types.hpp"

#include <algorithm>
#include <utility>

// ============================ RASTER ROOT ============================
std::shared_ptr<RasterRootNode> RasterRootNode::create(RasterData& r)
{
    return std::make_shared<RasterRootNode>(Private{}, r);
}

RasterCommitId RasterRootNode::recordEdit(GraphKey, const std::function<void(RasterEditor&)>& body)
{
    EditSummary s;
    const RasterCommitId commit = m_raster->recordEdit(body, s);
    publish(s);
    return commit;
}

void RasterRootNode::flip(GraphKey, RasterCommitId commit)
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
}

void RasterRootNode::publish(const EditSummary& s)
{
    if (s.touched.empty()) return;
    if (s.boundsChanged) invalidateBounds();
    emitDirty(s.touched, Evict::All);
}

// ============================ ONE INPUT ============================
std::shared_ptr<ReferenceNode> ReferenceNode::create()
{
    return std::make_shared<ReferenceNode>(Private{});
}

std::shared_ptr<MoveNode> MoveNode::create()
{
    return std::make_shared<MoveNode>(Private{});
}

TileSet MoveNode::setOffset(GraphKey, int x, int y)
{
    assert(TileUtils::inWorld(x) && TileUtils::inWorld(y));
    if (x == m_x && y == m_y) return {};
    return change([&] { m_x = x; m_y = y; });
}

void MoveNode::wetFootprint(TileSet& out)
{
    if (!input()) return;
    TileSet raw;
    input()->addFootprint(raw);
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
    if (!input()) return nullptr;
    if (((m_x | m_y) & TileUtils::kTileMask) == 0)   // whole-tile shift: forward, zero-copy
        return input()->fetch(tx - (m_x >> TileUtils::kTileShift), ty - (m_y >> TileUtils::kTileShift), ctx);

    Tile& out = ctx.take();   // sub-tile shift: up to 4 source tiles
    const TileSpan d = TileSpan::wholeTile(tx, ty);
    const size_t mk = ctx.mark();
    bool any = false;
    {
        Tile::BulkWrite w(out, Tile::BulkWrite::Trusted);
        forEachTileSpan(d.worldBounds().offsetBy(-m_x, -m_y), [&](const TileSpan& s) {
            if (const Tile* st = input()->fetch(s.tx, s.ty, ctx)) {
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
    return std::make_shared<CompositorNode>(Private{});
}

BoundsI CompositorNode::wetBounds() const
{
    BoundsI r;
    for (auto& l : layers()) r.expand(l->bounds());
    return r;
}

void CompositorNode::enableCache(GraphKey, bool on)
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
    return composeStack(layers().data(), layers().data() + layers().size(), tx, ty, ctx);
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

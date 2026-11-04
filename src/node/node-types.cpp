#include "node-types.hpp"

#include <algorithm>
#include <cstring>
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
namespace {

const GroupLook* lookOf(const std::vector<GroupLook>& looks, GroupKey key)
{
    for (const GroupLook& l : looks)
        if (l.key == key) return &l;
    return nullptr;
}

void addTiles(const BoundsI& b, TileSet& out)
{
    forEachTileSpan(b, [&](const TileSpan& s) { out.insert(TileUtils::packKey(s.tx, s.ty)); });
}

}

std::shared_ptr<CompositorNode> CompositorNode::create()
{
    return std::make_shared<CompositorNode>(Private{});
}

bool CompositorNode::muted(const Node& layer) const
{
    return std::binary_search(m_settings.muted.begin(), m_settings.muted.end(), layer.id());
}

void CompositorNode::setSettings(GraphKey, const NodeSettings& s)
{
    const auto* c = std::get_if<CompositorSettings>(&s);
    if (!c) return;
    // Kept to what it holds: mutes of its own layers, each once; groups as a stack holds them.
    CompositorSettings next;
    for (NodeId id : c->muted)
        if (std::any_of(layers().begin(), layers().end(), [&](const NodePtr& l) { return l->id() == id; })) next.muted.push_back(id);
    std::sort(next.muted.begin(), next.muted.end());
    next.muted.erase(std::unique(next.muted.begin(), next.muted.end()), next.muted.end());
    next.groups = StackGroups::normalized(c->groups, layers().size());
    if (next == m_settings) return;
    changeDrawing([&] { m_settings = std::move(next); });
}

void CompositorNode::setLooks(GraphKey, std::vector<GroupLook> looks)
{
    if (looks == m_looks) return;
    changeDrawing([&] { m_looks = std::move(looks); });
}

void CompositorNode::layersEdited(const LayerEdit& e)
{
    std::vector<StackGroup>& groups = m_settings.groups;
    if (const auto* in = std::get_if<LayerInserted>(&e))        StackGroups::rowInserted(groups, in->at);
    else if (const auto* out = std::get_if<LayerRemoved>(&e))   StackGroups::rowRemoved(groups, out->at);
    else if (const auto* move = std::get_if<LayerMoved>(&e))    StackGroups::rowMoved(groups, move->from, move->to);
    else if (std::holds_alternative<LayersCleared>(e))          StackGroups::rowsCleared(groups);
    // A layer that went takes its mute with it: linked again, it shows.
    std::erase_if(m_settings.muted, [&](NodeId id) {
        return std::none_of(layers().begin(), layers().end(), [&](const NodePtr& l) { return l->id() == id; });
    });
    refresh();
}

void CompositorNode::movedLayerTiles(size_t, size_t, const TileSet& moved, TileSet& dirty) const
{
    // Crossing a fill changes it where they overlap; in or out of a hidden group, everywhere it is.
    const bool hiding = std::any_of(m_looks.begin(), m_looks.end(), [](const GroupLook& l) { return l.hidden; });
    for (TileKey k : moved) {
        if (hiding) { dirty.insert(k); continue; }
        const auto t = TileUtils::unpackKey(k);
        for (const Drawn& d : m_drawn)
            if (d.row < 0 && TileUtils::tileOverlaps(d.fill, t.x, t.y)) { dirty.insert(k); break; }
    }
}

void CompositorNode::refresh()
{
    const std::vector<NodePtr>& rows = layers();
    m_shown.assign(rows.size(), 1);
    for (size_t i = 0; i < rows.size(); ++i)
        if (muted(*rows[i])) m_shown[i] = 0;
    for (const StackGroup& g : m_settings.groups)
        if (const GroupLook* l = lookOf(m_looks, g.key); l && l->hidden)
            for (uint32_t i = g.begin; i < g.end && i < rows.size(); ++i) m_shown[i] = 0;

    // Bottom first: a group's fill, then its rows.
    m_drawn.clear();
    for (const StackGroups::Entry& e : StackGroups::entries(m_settings.groups, rows.size())) {
        if (e.group != kNoGroup) {
            const GroupLook* l = lookOf(m_looks, e.group);
            if (l && !l->hidden && !l->fill.empty() && l->color.a > 0)
                m_drawn.push_back({ -1, e.group, e.begin, l->fill, premultiply(l->color), l->color });
        }
        for (uint32_t row = e.begin; row < e.end; ++row)
            if (m_shown[row]) m_drawn.push_back({ int(row), kNoGroup, 0, {}, {}, {} });
    }
}

void CompositorNode::changeDrawing(const std::function<void()>& mutate)
{
    const std::vector<char>  shownWas = m_shown;
    const std::vector<Drawn> drawnWas = m_drawn;
    mutate();
    refresh();
    // Where a layer came or went, and every fill that isn't the same, in the same place.
    TileSet dirty;
    for (size_t i = 0; i < layers().size(); ++i)
        if (i >= shownWas.size() || shownWas[i] != m_shown[i]) layers()[i]->addFootprint(dirty);
    const auto same = [](const Drawn& a, const Drawn& b) {
        return a.key == b.key && a.at == b.at && a.fill == b.fill && a.color == b.color;
    };
    const auto fillsOnly = [&](const std::vector<Drawn>& from, const std::vector<Drawn>& other) {
        for (const Drawn& d : from)
            if (d.row < 0 && std::none_of(other.begin(), other.end(), [&](const Drawn& o) { return o.row < 0 && same(d, o); }))
                addTiles(d.fill, dirty);
    };
    fillsOnly(drawnWas, m_drawn);
    fillsOnly(m_drawn, drawnWas);
    changeKnown(std::move(dirty), Bounds::Recompute, Evict::All, [] {});
}

BoundsI CompositorNode::wetBounds() const
{
    BoundsI r;
    for (const Drawn& d : m_drawn) r.expand(d.row >= 0 ? layers()[size_t(d.row)]->bounds() : d.fill);
    return r;
}

void CompositorNode::wetFootprint(TileSet& out)
{
    for (const Drawn& d : m_drawn) {
        if (d.row >= 0) layers()[size_t(d.row)]->addFootprint(out);
        else            addTiles(d.fill, out);
    }
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
    // Top down, accumulating *under* what's there (layers and fills alike, Over); stops once every
    // pixel is opaque.
    Tile& acc = ctx.take();   // cleared
    const size_t mk = ctx.mark();
    int  opaque = 0;
    bool any = false;
    {
        Tile::BulkWrite w(acc, Tile::BulkWrite::Trusted);
        RGBA* d = w.data();
        for (auto it = m_drawn.rbegin(); it != m_drawn.rend() && opaque < Tile::kPixels; ++it) {
            if (it->row < 0) {   // a fill: its colour where its rect covers the tile
                if (!TileUtils::tileOverlaps(it->fill, tx, ty)) continue;
                const int wx = TileUtils::tileToWorld(tx), wy = TileUtils::tileToWorld(ty);
                const int lx0 = std::max(0, it->fill.minX - wx), lx1 = std::min(Tile::kSize - 1, it->fill.maxX - wx);
                const int ly0 = std::max(0, it->fill.minY - wy), ly1 = std::min(Tile::kSize - 1, it->fill.maxY - wy);
                for (int ly = ly0; ly <= ly1; ++ly)
                    for (int lx = lx0; lx <= lx1; ++lx) {
                        RGBA& px = d[ly * Tile::kSize + lx];
                        if (px.a == 255) continue;
                        px = over(it->color, px);   // what's accumulated sits on top of the fill
                        opaque += px.a == 255;
                    }
                any = true;
                continue;
            }
            if (const Tile* src = layers()[size_t(it->row)]->fetch(tx, ty, ctx)) {
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

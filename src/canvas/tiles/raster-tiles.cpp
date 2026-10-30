#include "canvas/tiles/raster-tiles.hpp"
#include "canvas/tiles/downsample.hpp"

RasterTiles::RasterTiles(std::unique_ptr<TileSource> source, AtlasPages& pages, RenderCtx& render)
    : m_source(std::move(source)), m_render(&render)
{
    for (Level& lv : m_levels) lv.atlas.share(pages);
}

TileKey RasterTiles::parentKey(TileKey key, int levels)
{
    const auto t = TileUtils::unpackKey(key);
    return TileUtils::packKey(t.x >> levels, t.y >> levels);   // arithmetic shift: floor
}

void RasterTiles::sync()
{
    TileSet changed;
    if (m_source->takeChanges(changed) || m_fullSync) {
        restart();
        return;
    }
    for (TileKey key : changed) markStale(key);
}

void RasterTiles::restart()
{
    m_fullSync = false;
    for (Level& lv : m_levels) {
        lv.atlas.clear();
        lv.current.clear();
        lv.built.clear();
        lv.cpu.clear();
        lv.pending.clear();
        lv.children.clear();
        lv.footprint.clear();
        lv.footprintReady = false;
        ++lv.revision;
    }
    m_levels[0].footprint      = m_source->footprint();
    m_levels[0].footprintReady = true;
    ++m_revision;
}

void RasterTiles::markStale(TileKey key)
{
    bool added[kLevels] = {};   // the tile over `key` is new in that level's footprint
    added[0] = m_levels[0].footprint.insert(key).second;
    m_levels[0].current.erase(key);   // the old pixels stay in the atlas until rebuilt
    for (int l = 1; l < kLevels; ++l) {
        Level& lv = m_levels[l];
        if (!lv.footprintReady) continue;   // never asked for: worked out from below when it is
        const int base = baseOf(l);
        const TileKey tile = parentKey(key, l), child = parentKey(key, base);
        added[l] = lv.footprint.insert(tile).second;
        if (added[base]) lv.children[tile].push_back(child);
        lv.current.erase(tile);
        if (lv.built.contains(tile) || lv.pending.contains(tile)) lv.pending[tile].insert(child);   // refolds that part only
    }
    ++m_revision;
}

const TileSet& RasterTiles::footprint(int level)
{
    Level& lv = m_levels[level];
    if (!lv.footprintReady) {
        const int base = baseOf(level);
        for (TileKey k : footprint(base)) {
            const TileKey tile = parentKey(k, level - base);
            lv.footprint.insert(tile);
            lv.children[tile].push_back(k);
        }
        lv.footprintReady = true;
    }
    return lv.footprint;
}

bool RasterTiles::build(const Deadline& budget, const CanvasPipelines& p, int level, TileKey key)
{
    Level& lv = m_levels[level];
    if (lv.current.contains(key)) return true;
    if (budget.expired()) return false;
    if (level > 0) return fold(budget, p, level, key);
    const auto t = TileUtils::unpackKey(key);
    if (const Tile* tile = m_source->tile(t.x, t.y, *m_render)) lv.atlas.upload(p, key, tile->getReadAll());
    else                                                        lv.atlas.free(key);
    lv.current.insert(key);
    lv.built.insert(key);
    ++lv.revision;
    ++m_revision;
    return true;
}

bool RasterTiles::fold(const Deadline& budget, const CanvasPipelines& p, int level, TileKey key)
{
    footprint(level);
    Level& lv = m_levels[level];
    const int base = baseOf(level), span = level - base;
    auto [pending, first] = lv.pending.try_emplace(key);
    if (first && !lv.built.contains(key))   // never built: everything under it
        if (const auto c = lv.children.find(key); c != lv.children.end()) pending->second.insert(c->second.begin(), c->second.end());

    const auto [tx, ty] = TileUtils::unpackKey(key);
    Tile* dst = lv.cpu.getWriteTile(tx, ty);
    TileSet& todo = pending->second;   // only this level's map is ours; building the base touches another
    for (auto it = todo.begin(); it != todo.end();) {
        if (budget.expired()) return false;   // resumes here
        const TileKey child = *it;
        const auto c = TileUtils::unpackKey(child);
        const Tile* src = nullptr;
        if (base == 0) {
            src = m_source->tile(c.x, c.y, *m_render);
        } else {
            if (!build(budget, p, base, child)) return false;
            src = m_levels[base].cpu.getReadTile(c.x, c.y);
        }
        if (src || dst) {   // nothing before and nothing now: nothing to write
            if (!dst) {
                dst = &lv.cpu.writeOrCreateTile(tx, ty);
                dst->clear();
            }
            Tile::BulkWrite w(*dst, Tile::BulkWrite::Trusted);   // averages of premultiplied pixels stay premultiplied
            downsampleInto(w.data(), src, span, c.x - (tx << span), c.y - (ty << span));
            w.markChanged();
        }
        todo.erase(it++);
    }
    lv.pending.erase(key);
    lv.built.insert(key);
    lv.current.insert(key);
    if (dst && !dst->isEmpty()) {
        dst->settle();   // later const reads don't write
        lv.atlas.upload(p, key, dst->getReadAll());
    } else {
        if (dst) lv.cpu.freeTile(tx, ty);
        lv.atlas.free(key);
    }
    ++lv.revision;
    ++m_revision;
    return true;
}

size_t RasterTiles::tileCount() const
{
    size_t n = 0;
    for (const Level& lv : m_levels) n += lv.atlas.tileCount();
    return n;
}

void RasterTiles::release()
{
    for (Level& lv : m_levels) {
        lv.atlas.clear();
        lv.current.clear();
        lv.built.clear();
        lv.cpu.clear();
        lv.pending.clear();
        ++lv.revision;
    }
    m_fullSync = true;   // used again: start from scratch
    ++m_revision;
}

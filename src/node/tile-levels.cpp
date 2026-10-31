#include "node/tile-levels.hpp"

TileKey TileLevels::parentKey(TileKey key, int levels)
{
    const auto t = TileUtils::unpackKey(key);
    return TileUtils::packKey(t.x >> levels, t.y >> levels);   // arithmetic shift: floor
}

bool TileLevels::sync()
{
    TileSet changed;
    if (m_source->takeChanges(changed) || m_fullSync) {
        restart();
        return true;
    }
    for (TileKey key : changed) markStale(key);
    return false;
}

void TileLevels::clear()
{
    for (Level& lv : m_levels) {
        lv.current.clear();
        lv.built.clear();
        lv.cpu.clear();
        lv.pending.clear();
        ++lv.revision;
    }
    m_fullSync = true;   // used again: from scratch
    ++m_revision;
}

void TileLevels::restart()
{
    clear();
    for (Level& lv : m_levels) {
        lv.children.clear();
        lv.footprint.clear();
        lv.footprintReady = false;
    }
    m_fullSync = false;
    m_levels[0].footprint      = m_source->footprint();
    m_levels[0].footprintReady = true;
}

void TileLevels::markStale(TileKey key)
{
    bool added[kLevels] = {};   // the tile over `key` is new in that level's footprint
    added[0] = m_levels[0].footprint.insert(key).second;
    m_levels[0].current.erase(key);
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

const TileSet& TileLevels::footprint(int level)
{
    Level& lv = m_levels[level];
    if (!lv.footprintReady && level > 0) {   // level 0's is the source's, from sync()
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

bool TileLevels::build(const Deadline& budget, int level, TileKey key, const Built& built)
{
    Level& lv = m_levels[level];
    if (lv.current.contains(key)) return true;
    if (budget.expired()) return false;
    if (level > 0) return fold(budget, level, key, built);
    const auto t = TileUtils::unpackKey(key);
    built(0, key, m_source->tile(t.x, t.y, *m_render));
    lv.current.insert(key);
    lv.built.insert(key);
    ++lv.revision;
    ++m_revision;
    return true;
}

bool TileLevels::fold(const Deadline& budget, int level, TileKey key, const Built& built)
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
            if (!build(budget, base, child, built)) return false;
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
        built(level, key, dst);
    } else {
        if (dst) lv.cpu.freeTile(tx, ty);
        built(level, key, nullptr);
    }
    ++lv.revision;
    ++m_revision;
    return true;
}

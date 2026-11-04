#include "raster/tile-pool.hpp"
#include "raster/tile-codec.hpp"

#include <cassert>

std::unique_ptr<Tile> TilePool::newRaw() const
{
    ++m_raw;
    if (m_spares.empty()) return std::make_unique<Tile>();
    std::unique_ptr<Tile> t = std::move(m_spares.back());
    m_spares.pop_back();
    t->clear();
    return t;
}

void TilePool::dropRaw(Record& r) const
{
    if (!r.raw) return;
    --m_raw;
    if (m_spares.size() < kSpares) m_spares.push_back(std::move(r.raw));
    r.raw.reset();
}

TileId TilePool::allocate()
{
    TileId id;
    if (!m_free.empty()) {
        id = m_free.back();
        m_free.pop_back();
    } else {
        id = TileId(m_records.size());
        m_records.emplace_back();
    }
    Record& r = m_records[id];
    r.holders = 1;
    r.raw = newRaw();
    return id;
}

void TilePool::hold(TileId id)
{
    assert(m_records[id].holders > 0 && "a tile that's free");
    ++m_records[id].holders;
}

void TilePool::release(TileId id)
{
    Record& r = m_records[id];
    assert(r.holders > 0 && "released more than held");
    if (--r.holders > 0) return;
    dropRaw(r);
    m_cold -= r.bytes.size();
    r.bytes = {};
    m_free.push_back(id);
}

const Tile& TilePool::getRead(TileId id) const
{
    const Record& r = m_records[id];
    assert(r.holders > 0 && "a tile that's free");
    if (!r.raw) {   // cold: decoded now
        r.raw = newRaw();
        ByteReader in(r.bytes);
        [[maybe_unused]] const bool ok = decodeTile(in, *r.raw);
        assert(ok && "a pool's own bytes didn't decode");
    }
    return *r.raw;
}

Tile& TilePool::getWrite(TileId id)
{
    Record& r = m_records[id];
    assert(r.holders == 1 && "written while shared (copy it first)");
    (void)getRead(id);
    m_cold -= r.bytes.size();
    r.bytes = {};   // stale from now on
    return *r.raw;
}

void TilePool::cool(TileId id)
{
    Record& r = m_records[id];
    if (r.holders == 0 || !r.raw) return;
    if (r.bytes.empty()) {   // encoded once: kept from then on
        ByteWriter out;
        encodeTile(*r.raw, out);
        r.bytes = out.take();
        m_cold += r.bytes.size();
    }
    dropRaw(r);
}

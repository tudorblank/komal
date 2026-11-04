#pragma once
// The tiles of one raster's grids (its live grid and every snapshot of it), each one kept once and
// held by however many pages point at it (TileGrid): the last page to let go frees it. A tile is
// written only while one page holds it; a grid copies it first otherwise (copy-on-write).
//
// A tile is raw (its pixels, readable) or cold (only its bytes, as raster/tile-codec writes them).
// Reading a cold one makes it raw again (decoded on the spot). Cooling a raw one encodes it once
// and lets its pixels go; the bytes stay after that, so a tile warmed and cooled again is never
// encoded twice. A write makes its bytes stale: they go.
//
//   TileId id = pool.allocate();   // clear, raw, held once
//   pool.cool(id);                 // 16 KB -> its bytes
//   pool.getRead(id);              // raw again

#include "raster/tile.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using TileId = uint32_t;
constexpr TileId kNoTileId = 0xFFFFFFFFu;

class TilePool {
public:
    TilePool() = default;
    TilePool(const TilePool&) = delete;
    TilePool& operator=(const TilePool&) = delete;

    TileId allocate();              // a new tile: clear, raw, held once
    void   hold(TileId id);         // one more page points at it
    void   release(TileId id);      // one less: the last one frees it
    uint32_t holders(TileId id) const { return m_records[id].holders; }

    const Tile& getRead(TileId id) const;   // raw (a cold one is decoded now)
    Tile&       getWrite(TileId id);        // raw, held once: its bytes go (they'd be stale)

    bool isRaw(TileId id) const { return m_records[id].raw != nullptr; }
    void cool(TileId id);           // its bytes (encoded once), its pixels let go
    void warm(TileId id) const { (void)getRead(id); }

    size_t liveCount() const { return m_records.size() - m_free.size(); }   // tiles held
    template<class F> void forEachHeld(F&& f) const   // every tile held, by id
    {
        for (TileId id = 0; id < TileId(m_records.size()); ++id) if (m_records[id].holders > 0) f(id);
    }
    size_t rawBytes() const  { return m_raw * sizeof(Tile); }               // pixels in memory
    size_t coldBytes() const { return m_cold; }                             // encoded bytes kept

private:
    struct Record {
        uint32_t holders = 0;
        mutable std::unique_ptr<Tile> raw;   // its pixels (null: cold)
        std::vector<uint8_t> bytes;          // encoded (empty: never cooled, or written since)
    };
    std::unique_ptr<Tile> newRaw() const;   // a clear buffer: a spare one if there is
    void dropRaw(Record& r) const;

    static constexpr size_t kSpares = 64;   // raw buffers kept to reuse (1 MB)
    mutable std::vector<Record> m_records;
    std::vector<TileId> m_free;
    mutable std::vector<std::unique_ptr<Tile>> m_spares;
    mutable size_t m_raw = 0;    // tiles raw now
    size_t m_cold = 0;           // bytes kept, cooled tiles'
};

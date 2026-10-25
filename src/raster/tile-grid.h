#pragma once
// Sparse tile storage: tiles live in a pool, pages of 16x16 slots map tile coords to pool ids.

#include "raster/tile.h"

#include <memory>
#include <vector>

// === TILE POOL === //
using TileId = uint32_t;
constexpr TileId kNoTileId = 0xFFFFFFFFu;

class TilePool {
public:
    TileId allocate()
    {
        if (!m_freeIds.empty()) {   // reuse released ones first
            const TileId id = m_freeIds.back();
            m_freeIds.pop_back();
            return id;
        }
        const TileId id = m_nextFreshId;
        if ((id >> kBlockShift) >= m_blocks.size())   // new block when the last one is full
            m_blocks.push_back(std::make_unique<Tile[]>(kBlockTiles));
        ++m_nextFreshId;
        return id;
    }
    void release(TileId id)
    {
        assert(id < m_nextFreshId);
        getWrite(id).clear();
        m_freeIds.push_back(id);
    }

    const Tile& getRead(TileId id) const { return m_blocks[id >> kBlockShift][id & kBlockMask]; }
    Tile&       getWrite(TileId id)      { return m_blocks[id >> kBlockShift][id & kBlockMask]; }
    size_t liveCount() const { return m_nextFreshId - m_freeIds.size(); }

    void clear()
    {
        m_blocks.clear();
        m_freeIds.clear();
        m_nextFreshId = 0;
    }

private:
    static constexpr uint32_t kBlockShift = 4;
    static constexpr uint32_t kBlockTiles = 1u << kBlockShift;   // 16 tiles (256 KB) per block
    static constexpr uint32_t kBlockMask  = kBlockTiles - 1;

    std::vector<std::unique_ptr<Tile[]>> m_blocks;   // blocks never move: tile addresses are stable
    std::vector<TileId> m_freeIds;
    uint32_t m_nextFreshId = 0;
};

// === TILE GRID === //
class TileGrid {
    static constexpr int kPageSlots = TileUtils::kPageDim * TileUtils::kPageDim;
    struct Page {
        TileId slots[kPageSlots];
        int live = 0;
        Page() { std::fill(std::begin(slots), std::end(slots), kNoTileId); }
    };
    static constexpr PageKey pageKeyOf(int tx, int ty)
    { return TileUtils::packKey(TileUtils::tileToPage(tx), TileUtils::tileToPage(ty)); }
    static constexpr int pageSlotOf(int tx, int ty)
    { return TileUtils::tileToPageLocal(ty) * TileUtils::kPageDim + TileUtils::tileToPageLocal(tx); }

public:
    // Cached reader (stateful, single-threaded): consecutive reads in the same page skip the
    // hash lookup. Must not outlive a write to the grid (checked in debug builds).
    class ReadAccessor {
    public:
        explicit ReadAccessor(const TileGrid& g) : m_g(&g), m_epoch(g.m_epoch) {}
        const Tile* get(int tx, int ty)
        {
            assert(m_epoch == m_g->m_epoch && "accessor used after the grid was written to");
            const PageKey k = pageKeyOf(tx, ty);
            if (k != m_key) {
                m_page = m_g->findPage(k);   // nullptr if missing; cached either way
                m_key  = k;
            }
            if (!m_page) return nullptr;
            const TileId id = m_page->slots[pageSlotOf(tx, ty)];
            return id == kNoTileId ? nullptr : &m_g->m_pool.getRead(id);
        }
        RGBA pixel(int wx, int wy)
        {
            const Tile* t = get(TileUtils::worldToTile(wx), TileUtils::worldToTile(wy));
            return t ? t->getReadPx(TileUtils::worldToLocal(wx), TileUtils::worldToLocal(wy)) : transparent();
        }

    private:
        const TileGrid* m_g;
        uint32_t    m_epoch;
        PageKey     m_key  = kNoKey;   // no page has this key
        const Page* m_page = nullptr;
    };
    ReadAccessor reader() const { return ReadAccessor(*this); }

    // one-off lookups
    const Tile* getReadTile(int tx, int ty) const
    {
        const TileId id = tileIdAt(tx, ty);
        return id == kNoTileId ? nullptr : &m_pool.getRead(id);
    }
    size_t size() const { return m_pool.liveCount(); }

    // write
    Tile* getWriteTile(int tx, int ty)
    {
        ++m_epoch;
        const TileId id = tileIdAt(tx, ty);
        return id == kNoTileId ? nullptr : &m_pool.getWrite(id);
    }
    Tile& writeOrCreateTile(int tx, int ty)
    {
        ++m_epoch;
        const PageKey pk = pageKeyOf(tx, ty);
        auto& page = m_pages[pk];
        if (!page) page = std::make_unique<Page>();

        TileId& slot = page->slots[pageSlotOf(tx, ty)];
        if (slot == kNoTileId) {
            slot = m_pool.allocate();
            ++page->live;
        }
        return m_pool.getWrite(slot);
    }
    bool freeTile(int tx, int ty)
    {
        ++m_epoch;
        const PageKey pk = pageKeyOf(tx, ty);
        auto it = m_pages.find(pk);
        if (it == m_pages.end()) return false;
        TileId& id = it->second->slots[pageSlotOf(tx, ty)];
        if (id == kNoTileId) return false;

        m_pool.release(id);
        id = kNoTileId;
        if (--it->second->live == 0) m_pages.erase(it);   // empty pages go away
        return true;
    }
    void clear()
    {
        ++m_epoch;
        m_pages.clear();
        m_pool.clear();
    }

    // visit every tile (page order, no per-tile hash lookup)
    template<class F> void forEachRead(F&& f) const
    {
        for (const auto& [pageKey, page] : m_pages) {
            const TileUtils::KeyXY p = TileUtils::unpackKey(pageKey);
            for (int s = 0; s < kPageSlots; ++s) {
                if (page->slots[s] == kNoTileId) continue;
                f(p.x * TileUtils::kPageDim + (s & TileUtils::kPageMask),
                  p.y * TileUtils::kPageDim + (s >> TileUtils::kPageShift),
                  m_pool.getRead(page->slots[s]));
            }
        }
    }
    template<class F> void forEachWrite(F&& f)
    {
        ++m_epoch;
        for (auto& [pageKey, page] : m_pages) {
            const TileUtils::KeyXY p = TileUtils::unpackKey(pageKey);
            for (int s = 0; s < kPageSlots; ++s) {
                if (page->slots[s] == kNoTileId) continue;
                f(p.x * TileUtils::kPageDim + (s & TileUtils::kPageMask),
                  p.y * TileUtils::kPageDim + (s >> TileUtils::kPageShift),
                  m_pool.getWrite(page->slots[s]));
            }
        }
    }

private:
    const Page* findPage(PageKey k) const
    {
        auto it = m_pages.find(k);
        return it == m_pages.end() ? nullptr : it->second.get();
    }
    TileId tileIdAt(int tx, int ty) const
    {
        const Page* p = findPage(pageKeyOf(tx, ty));
        return p ? p->slots[pageSlotOf(tx, ty)] : kNoTileId;
    }

    TilePool m_pool;
    PageMap<std::unique_ptr<Page>> m_pages;
    uint32_t m_epoch = 0;   // bumped on any write access
};
using TileReader = TileGrid::ReadAccessor;

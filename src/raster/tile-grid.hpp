#pragma once
// Sparse tile storage: tiles live in a pool (TilePool), pages of 16x16 slots map tile coords to
// pool ids.
//
// A copy of a grid is a snapshot, made at the cost of its page map: it shares the pool, the pages
// and the tiles with the grid it came from. Writing to either copies only what it touches first
// (copy-on-write): a page still shared, then a tile still held by another page. So any number of
// snapshots of a raster (its history) cost what they don't share, and none sees the others' writes.
//
//   TileGrid live;
//   const TileGrid before = live;          // shares everything
//   live.writeOrCreateTile(3, 4) ...       // that tile's page and the tile copied: `before` as it was

#include "raster/tile-pool.hpp"

#include <cassert>
#include <memory>
#include <utility>
#include <vector>

// === TILE GRID === //
class TileGrid {
    static constexpr int kPageSlots = TileUtils::kPageDim * TileUtils::kPageDim;
    // A page holds each of its tiles once (TilePool::hold): copied, it holds them again.
    struct Page {
        TilePool* pool;
        TileId slots[kPageSlots];
        int live = 0;
        explicit Page(TilePool* p) : pool(p) { std::fill(std::begin(slots), std::end(slots), kNoTileId); }
        Page(const Page& o) : pool(o.pool), live(o.live)
        {
            std::copy(std::begin(o.slots), std::end(o.slots), std::begin(slots));
            for (TileId id : slots) if (id != kNoTileId) pool->hold(id);
        }
        Page& operator=(const Page&) = delete;
        ~Page() { for (TileId id : slots) if (id != kNoTileId) pool->release(id); }
    };
    static constexpr PageKey pageKeyOf(int tx, int ty)
    { return PageKey(TileUtils::packKey(TileUtils::tileToPage(tx), TileUtils::tileToPage(ty))); }
    static constexpr PageKey kNoPage = PageKey(kNoKey);   // no page is this far out
    static constexpr int pageSlotOf(int tx, int ty)
    { return TileUtils::tileToPageLocal(ty) * TileUtils::kPageDim + TileUtils::tileToPageLocal(tx); }

public:
    TileGrid() : m_pool(std::make_shared<TilePool>()) {}   // a pool of its own
    // A snapshot: the same pool, pages and tiles, until either writes.
    TileGrid(const TileGrid& o) : m_pool(o.m_pool), m_pages(o.m_pages), m_size(o.m_size) {}
    TileGrid& operator=(const TileGrid& o)
    {
        if (this != &o) {
            ++m_epoch;
            m_pages = o.m_pages;   // before the pool: a page let go of needs its own
            m_pool  = o.m_pool;
            m_size  = o.m_size;
        }
        return *this;
    }
    // Moved from: empty, with a pool of its own again (usable like a new one).
    TileGrid(TileGrid&& o) noexcept : m_pool(std::exchange(o.m_pool, std::make_shared<TilePool>())),
                                      m_pages(std::exchange(o.m_pages, {})), m_size(std::exchange(o.m_size, 0)) { ++o.m_epoch; }
    TileGrid& operator=(TileGrid&& o) noexcept
    {
        if (this != &o) {
            ++m_epoch;
            ++o.m_epoch;
            m_pages = std::exchange(o.m_pages, {});   // ours let go into our pool first
            m_pool  = std::exchange(o.m_pool, std::make_shared<TilePool>());
            m_size  = std::exchange(o.m_size, 0);
        }
        return *this;
    }
    ~TileGrid() { m_pages.clear(); }   // the pages let go of their tiles while the pool is there

    // Cached reader: reads in the same page skip the hash lookup. Single-threaded; must not
    // outlive a write to the grid (checked in debug).
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
            return id == kNoTileId ? nullptr : &m_g->m_pool->getRead(id);
        }
        RGBA pixel(int wx, int wy)
        {
            const Tile* t = get(TileUtils::worldToTile(wx), TileUtils::worldToTile(wy));
            return t ? t->getReadPx(TileUtils::worldToLocal(wx), TileUtils::worldToLocal(wy)) : transparent();
        }

    private:
        const TileGrid* m_g;
        uint32_t    m_epoch;
        PageKey     m_key  = kNoPage;
        const Page* m_page = nullptr;
    };
    ReadAccessor reader() const { return ReadAccessor(*this); }

    // one-off lookups
    const Tile* getReadTile(int tx, int ty) const
    {
        const TileId id = tileIdAt(tx, ty);
        return id == kNoTileId ? nullptr : &m_pool->getRead(id);
    }
    size_t size() const { return m_size; }                   // its tiles
    size_t bytes() const { return m_size * sizeof(Tile); }   // their pixels, raw

    // write: what's touched is its own first (copy-on-write)
    Tile* getWriteTile(int tx, int ty)
    {
        ++m_epoch;
        Page* page = ownPage(pageKeyOf(tx, ty));
        if (!page) return nullptr;
        TileId& slot = page->slots[pageSlotOf(tx, ty)];
        if (slot == kNoTileId) return nullptr;
        return &m_pool->getWrite(ownTile(slot));
    }
    Tile& writeOrCreateTile(int tx, int ty)
    {
        ++m_epoch;
        Page& page = ownOrNewPage(pageKeyOf(tx, ty));
        TileId& slot = page.slots[pageSlotOf(tx, ty)];
        if (slot == kNoTileId) {
            slot = m_pool->allocate();
            ++page.live;
            ++m_size;
        }
        return m_pool->getWrite(ownTile(slot));
    }
    bool freeTile(int tx, int ty)
    {
        ++m_epoch;
        const PageKey pk = pageKeyOf(tx, ty);
        const Page* seen = findPage(pk);
        if (!seen || seen->slots[pageSlotOf(tx, ty)] == kNoTileId) return false;
        Page* page = ownPage(pk);   // there: seen above
        if (!page) return false;
        TileId& id = page->slots[pageSlotOf(tx, ty)];
        m_pool->release(id);
        id = kNoTileId;
        --m_size;
        if (--page->live == 0) m_pages.erase(pk);   // empty pages go away
        return true;
    }
    void clear()
    {
        ++m_epoch;
        m_pages.clear();
        m_size = 0;
    }

    // visit every tile (page order, no per-tile hash lookup)
    template<class F> void forEachRead(F&& f) const
    {
        for (const auto& [pageKey, page] : m_pages) {
            const TileUtils::KeyXY p = TileUtils::unpackKey(uint64_t(pageKey));
            for (int s = 0; s < kPageSlots; ++s) {
                if (page->slots[s] == kNoTileId) continue;
                f(p.x * TileUtils::kPageDim + (s & TileUtils::kPageMask),
                  p.y * TileUtils::kPageDim + (s >> TileUtils::kPageShift),
                  m_pool->getRead(page->slots[s]));
            }
        }
    }
    template<class F> void forEachWrite(F&& f)
    {
        ++m_epoch;
        for (auto& [pageKey, shared] : m_pages) {
            if (shared.use_count() > 1) shared = std::make_shared<Page>(*shared);   // its own first
            Page& page = *shared;
            const TileUtils::KeyXY p = TileUtils::unpackKey(uint64_t(pageKey));
            for (int s = 0; s < kPageSlots; ++s) {
                if (page.slots[s] == kNoTileId) continue;
                f(p.x * TileUtils::kPageDim + (s & TileUtils::kPageMask),
                  p.y * TileUtils::kPageDim + (s >> TileUtils::kPageShift),
                  m_pool->getWrite(ownTile(page.slots[s])));
            }
        }
    }

    // ---- snapshots
    bool sharesPoolWith(const TileGrid& o) const { return m_pool == o.m_pool; }
    // The tiles where this and `other` (a snapshot of the same pool) aren't the same tile: there
    // in one and not the other, or each its own. Pages they share are skipped whole.
    template<class F> void forEachDifferent(const TileGrid& other, F&& f) const
    {
        assert(sharesPoolWith(other));
        const auto visit = [&](PageKey key, const Page* a, const Page* b) {
            const TileUtils::KeyXY p = TileUtils::unpackKey(uint64_t(key));
            for (int s = 0; s < kPageSlots; ++s) {
                const TileId x = a ? a->slots[s] : kNoTileId, y = b ? b->slots[s] : kNoTileId;
                if (x != y) f(p.x * TileUtils::kPageDim + (s & TileUtils::kPageMask), p.y * TileUtils::kPageDim + (s >> TileUtils::kPageShift));
            }
        };
        for (const auto& [key, page] : m_pages) {
            const Page* theirs = other.findPage(key);
            if (theirs != page.get()) visit(key, page.get(), theirs);
        }
        for (const auto& [key, page] : other.m_pages)
            if (!findPage(key)) visit(key, nullptr, page.get());
    }
    // Every tile it holds, by pool id (what the pool's raw and cold work goes by).
    template<class F> void forEachTileId(F&& f) const
    {
        for (const auto& [key, page] : m_pages)
            for (TileId id : page->slots) if (id != kNoTileId) f(id);
    }
    TilePool& pool() const { return *m_pool; }   // shared by every snapshot of it

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
    // The page at `k`, this grid's alone (copied if another grid holds it too); null if there's none.
    Page* ownPage(PageKey k)
    {
        auto it = m_pages.find(k);
        if (it == m_pages.end()) return nullptr;
        if (it->second.use_count() > 1) it->second = std::make_shared<Page>(*it->second);
        return it->second.get();
    }
    Page& ownOrNewPage(PageKey k)   // ... made if there's none
    {
        if (Page* p = ownPage(k)) return *p;
        return *m_pages.emplace(k, std::make_shared<Page>(m_pool.get())).first->second;
    }
    // The tile in an own page's slot, copied if another page holds it too: the slot gets the copy.
    TileId ownTile(TileId& slot)
    {
        if (m_pool->holders(slot) > 1) {
            const TileId copy = m_pool->allocate();
            m_pool->getWrite(copy).copyFrom(m_pool->getRead(slot));
            m_pool->release(slot);
            slot = copy;
        }
        return slot;
    }

    std::shared_ptr<TilePool> m_pool;   // before the pages: they let go into it
    PageMap<std::shared_ptr<Page>> m_pages;
    size_t   m_size = 0;     // tiles
    uint32_t m_epoch = 0;    // bumped on any write access
};
using TileReader = TileGrid::ReadAccessor;

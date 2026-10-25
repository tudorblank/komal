#pragma once
// A raster (sparse tile grid + bounds), the one way to edit it (RasterEditor), and the tile
// snapshots history is made of (TileDelta).

#include "raster/tile-grid.h"

#include <exception>
#include <memory>
#include <span>
#include <vector>

// === HISTORY STEP === //
// Tiles as they were before an edit (nullptr state = the tile didn't exist). Applying a delta
// swaps every entry with the raster's current tile, so the same delta undoes and redoes.
struct TileDelta {
    struct Entry {
        TileKey key;
        std::unique_ptr<Tile> state;
    };
    std::vector<Entry> entries;

    void remember(TileKey key, const Tile* tile)
    {
        Entry& e = entries.emplace_back(Entry{ key, nullptr });
        if (tile) {
            e.state = std::make_unique<Tile>();
            e.state->copyFrom(*tile);
        }
    }
    void forgetLast() { entries.pop_back(); }
    bool empty() const { return entries.empty(); }

    size_t bytes() const
    {
        size_t n = 0;
        for (const Entry& e : entries) if (e.state) n += sizeof(Tile);
        return n;
    }
};

// What a committed edit touched: tiles to re-read, and whether the bounds moved.
struct EditSummary {
    TileSet  touched;
    bool     boundsChanged = false;
    uint64_t revision = 0;
};

// === RASTER DATA === //
using RasterId = uint32_t;
constexpr RasterId kNoRasterId = 0xFFFFFFFFu;
class RasterEditor;

class RasterData {
public:
    explicit RasterData(RasterId id) : m_id(id) {}
    RasterData(const RasterData&) = delete;
    RasterData& operator=(const RasterData&) = delete;
    RasterId id() const { return m_id; }

    // read
    const Tile* getReadTile(int tx, int ty) const { return m_grid.getReadTile(tx, ty); }
    RGBA getPixel(int wx, int wy) const { return reader().pixel(wx, wy); }
    TileReader reader() const { return m_grid.reader(); }
    template<class F> void forEachTile(F&& f) const { m_grid.forEachRead(std::forward<F>(f)); }

    // valid between edits only (an open RasterEditor makes them stale)
    const BoundsI& getBounds() const { assert(!m_pendingEdits); return m_bounds; }
    size_t tileCount() const { assert(!m_pendingEdits); return m_grid.size(); }
    uint64_t revision() const { return m_revision; }   // bumps on every commit that changed something

private:
    friend class RasterEditor;

    RasterId m_id;
    TileGrid m_grid;
    BoundsI  m_bounds;
    RasterEditor* m_editor = nullptr;
    bool     m_pendingEdits = false;
    uint64_t m_revision = 0;

    void recomputeBounds()
    {
        m_bounds.reset();
        m_grid.forEachRead([&](int tx, int ty, const Tile& t) {
            const BoundsI& lb = t.bounds();
            m_bounds.expand(TileUtils::tileToWorld(tx, lb.minX), TileUtils::tileToWorld(ty, lb.minY));
            m_bounds.expand(TileUtils::tileToWorld(tx, lb.maxX), TileUtils::tileToWorld(ty, lb.maxY));
        });
    }
};

// === GATHER === //
struct FlatImage {
    std::span<const RGBA> pixels;   // row by row, width * height entries, any alpha convention
    int width, height;

    const RGBA* rowFrom(int x, int y) const { return &pixels[size_t(y) * width + x]; }
};

// out = the part of `image` (placed at imageX, imageY) that covers `area`.
inline void gatherFromImage(Tile& out, const FlatImage& image, const TileSpan& area, int imageX, int imageY)
{
    out.clear();
    Tile::BulkWrite w(out, Tile::BulkWrite::Untrusted);   // outside data: made canonical on close
    const int firstImageX = area.tileWX + area.lx0 - imageX;
    const int rowLength   = area.lx1 - area.lx0 + 1;
    for (int row = area.ly0; row <= area.ly1; ++row) {
        const int imageRow = area.tileWY + row - imageY;
        std::memcpy(&w.data()[row * Tile::kSize + area.lx0], image.rowFrom(firstImageX, imageRow),
                    size_t(rowLength) * sizeof(RGBA));
    }
    w.markChanged();
}

// The image placed at (imageX, imageY), as tiles: canonical, stats settled, empty ones left
// out. Touches nothing but `out`, so the slow part of an import can run on another thread.
inline void tilesFromImage(TileGrid& out, const FlatImage& image, int imageX, int imageY)
{
    if (image.width <= 0 || image.height <= 0) return;
    forEachTileSpan(BoundsI{ imageX, imageY, imageX + image.width - 1, imageY + image.height - 1 }, [&](const TileSpan& piece) {
        Tile& t = out.writeOrCreateTile(piece.tx, piece.ty);
        gatherFromImage(t, image, piece, imageX, imageY);
        t.settle();
        if (t.isEmpty()) out.freeTile(piece.tx, piece.ty);
    });
}

// out = the source world shifted by (shiftX, shiftY), over destination tile `destArea`.
// Returns false (and leaves `out` clear) when no source tile contributes.
inline bool gatherFromTiles(Tile& out, TileReader& source, const TileSpan& destArea, int shiftX, int shiftY)
{
    out.clear();
    const BoundsI sourceRegion = destArea.worldBounds().offsetBy(-shiftX, -shiftY);
    bool any = false;
    {
        Tile::BulkWrite w(out, Tile::BulkWrite::Trusted);   // committed tiles are already canonical
        forEachTileSpan(sourceRegion, [&](const TileSpan& piece) {   // up to 4 source tiles
            if (const Tile* src = source.get(piece.tx, piece.ty)) {
                copySpanShifted(w.data(), *src, piece, destArea, shiftX, shiftY);
                w.markChanged();
                any = true;
            }
        });
    }   // closing the writer marks the stats stale
    return any;
}

// === RASTER EDITOR === //
// One edit session on a raster. Edits are tracked per tile; commit() frees tiles that became
// empty, updates bounds and the revision, and reports what changed.
class RasterEditor {
public:
    explicit RasterEditor(RasterData& r) : m_r(r)
    {
        assert(!r.m_editor && "one RasterEditor per raster at a time");
        r.m_editor = this;
    }
    RasterEditor(const RasterEditor&) = delete;
    RasterEditor& operator=(const RasterEditor&) = delete;
    ~RasterEditor()
    {
        if (!m_touched.empty()) {
            assert(std::uncaught_exceptions() > 0 && "RasterEditor destroyed with uncommitted edits");
            (void)commit();
        }
        m_r.m_editor = nullptr;
    }

    // Snapshot every tile before its first change into `step` (for undo).
    void record(TileDelta* step)
    {
        assert(m_touched.empty() && "attach the recorder before the first edit");
        m_rec = step;
    }

    // --- wipe
    void clearAll()
    {
        m_r.m_grid.forEachRead([&](int tx, int ty, const Tile& tile) {
            const TileKey key = TileUtils::packKey(tx, ty);
            if (m_touched.insert(key).second && m_rec) m_rec->remember(key, &tile);
        });
        m_r.m_grid.clear();
        m_lastTouched = kNoKey;
        m_r.m_pendingEdits = !m_touched.empty();
    }

    // --- per pixel
    bool setPixel(int x, int y, RGBA color)
    {
        assert(TileUtils::inWorld(x) && TileUtils::inWorld(y));
        const int lx = TileUtils::worldToLocal(x), ly = TileUtils::worldToLocal(y);
        return editTile(TileUtils::worldToTile(x), TileUtils::worldToTile(y), color.a != 0,   // invisible pixels only erase
                        [&](Tile& t) { return t.setPx(lx, ly, color); });
    }
    bool erasePixel(int x, int y) { return setPixel(x, y, transparent()); }

    // --- whole tile
    bool fillTile(int tx, int ty, RGBA color)
    { return editTile(tx, ty, color.a != 0, [&](Tile& t) { return TileEdit::fill(t, color); }); }
    bool eraseTile(int tx, int ty)
    { return editExisting(tx, ty, [](Tile& t) { return TileEdit::clear(t); }); }

    // --- tile onto tile
    bool applyTile(int tx, int ty, const Tile& source, CompositeParams params)
    {
        const bool replacing = params.mode == CompositeMode::Replace;
        if (!replacing && (params.opacity == 0 || source.isEmpty())) return false;
        return editTile(tx, ty, params.mode != CompositeMode::Erase,
                        [&](Tile& dest) { return TileEdit::apply(dest, source, params); });
    }
    void applyTileGrid(const TileGrid& grid, CompositeParams params)
    {
        grid.forEachRead([&](int tx, int ty, const Tile& source) { applyTile(tx, ty, source, params); });
    }

    // --- rectangles / images
    void clearRegion(const BoundsI& area)
    {
        if (area.empty()) return;
        assert(TileUtils::inWorld(area.minX) && TileUtils::inWorld(area.maxX) &&
               TileUtils::inWorld(area.minY) && TileUtils::inWorld(area.maxY));
        forEachTileSpan(area, [&](const TileSpan& piece) {
            editExisting(piece.tx, piece.ty, [&](Tile& t) {
                return piece.isWholeTile() ? TileEdit::clear(t) : TileEdit::clearRect(t, piece);
            });
        });
    }
    void blit(const FlatImage& image, int imageX, int imageY, CompositeParams params,
              const BoundsI* cropToImageRect = nullptr)
    {
        assert(params.mode != CompositeMode::Replace);
        assert(TileUtils::inWorld(imageX) && TileUtils::inWorld(imageY));
        assert(image.width > 0 && image.height > 0);
        assert(image.pixels.size() == size_t(image.width) * size_t(image.height));

        BoundsI imagePart{ 0, 0, image.width - 1, image.height - 1 };
        if (cropToImageRect) imagePart = imagePart.intersect(*cropToImageRect);
        if (imagePart.empty()) return;

        auto scratch = std::make_unique<Tile>();   // 16 KB: heap, not stack
        forEachTileSpan(imagePart.offsetBy(imageX, imageY), [&](const TileSpan& piece) {
            gatherFromImage(*scratch, image, piece, imageX, imageY);
            applyTile(piece.tx, piece.ty, *scratch, params);
        });
    }
    void paste(TileReader& source, const BoundsI& sourceArea, int shiftX, int shiftY, CompositeParams params)
    {
        if (sourceArea.empty()) return;
        assert(params.mode != CompositeMode::Replace);
        assert(TileUtils::inWorld(shiftX) && TileUtils::inWorld(shiftY));

        // Whole-tile shift: every source tile lines up with one destination tile, apply directly.
        const bool wholeTiles = ((shiftX | shiftY) & TileUtils::kTileMask) == 0;
        const int  dtx = shiftX >> TileUtils::kTileShift, dty = shiftY >> TileUtils::kTileShift;

        auto scratch = std::make_unique<Tile>();
        forEachTileSpan(sourceArea.offsetBy(shiftX, shiftY), [&](const TileSpan& piece) {
            if (wholeTiles && piece.isWholeTile()) {
                if (const Tile* src = source.get(piece.tx - dtx, piece.ty - dty))
                    applyTile(piece.tx, piece.ty, *src, params);
                return;
            }
            if (gatherFromTiles(*scratch, source, piece, shiftX, shiftY))
                applyTile(piece.tx, piece.ty, *scratch, params);
        });
    }

    // --- commit
    [[nodiscard]] EditSummary commit()
    {
        const BoundsI before = m_r.m_bounds;
        BoundsI grown = before;

        // Per side (0=minX 1=maxX 2=minY 3=maxY): could a touched tile have held the old
        // extreme, and does a touched tile still reach it? Shrinking needs a full recompute.
        bool cand[4]  = {};
        bool reach[4] = {};

        for (TileKey key : m_touched) {
            const auto [tx, ty] = TileUtils::unpackKey(key);
            if (!before.empty()) {
                cand[0] |= tx == TileUtils::worldToTile(before.minX);
                cand[1] |= tx == TileUtils::worldToTile(before.maxX);
                cand[2] |= ty == TileUtils::worldToTile(before.minY);
                cand[3] |= ty == TileUtils::worldToTile(before.maxY);
            }

            Tile* t = m_r.m_grid.getWriteTile(tx, ty);
            if (!t) continue;
            t->settle();
            if (t->isEmpty()) { m_r.m_grid.freeTile(tx, ty); continue; }

#ifndef NDEBUG
            const RGBA* px = t->getReadAll();
            for (int i = 0; i < Tile::kPixels; ++i) assert(canonical(px[i]) == px[i]);
#endif
            const BoundsI& lb = t->bounds();
            const int wMinX = TileUtils::tileToWorld(tx, lb.minX), wMaxX = TileUtils::tileToWorld(tx, lb.maxX);
            const int wMinY = TileUtils::tileToWorld(ty, lb.minY), wMaxY = TileUtils::tileToWorld(ty, lb.maxY);
            grown.expand(wMinX, wMinY);
            grown.expand(wMaxX, wMaxY);
            reach[0] |= wMinX == before.minX;  reach[1] |= wMaxX == before.maxX;
            reach[2] |= wMinY == before.minY;  reach[3] |= wMaxY == before.maxY;
        }

        bool mayShrink = false;
        for (int i = 0; i < 4; ++i) mayShrink |= cand[i] && !reach[i];
        if (mayShrink) m_r.recomputeBounds(); else m_r.m_bounds = grown;
        if (!m_touched.empty()) ++m_r.m_revision;

        EditSummary out{ std::move(m_touched), m_r.m_bounds != before, m_r.m_revision };
        m_touched.clear();
        m_lastTouched = kNoKey;
        m_r.m_pendingEdits = false;
        m_rec = nullptr;
        return out;
    }

private:
    RasterData& m_r;
    TileSet     m_touched;
    TileKey     m_lastTouched = kNoKey;   // skips the set lookup for runs of edits in one tile
    TileDelta*  m_rec = nullptr;

    template<class F> bool runEdit(int tx, int ty, Tile& t, bool existed, F&& f)
    {
        const TileKey k = TileUtils::packKey(tx, ty);
        const bool firstTouch = k != m_lastTouched && m_touched.insert(k).second;
        if (firstTouch && m_rec) m_rec->remember(k, existed ? &t : nullptr);

        const bool changed = f(t);
        if (changed) m_lastTouched = k;
        else if (firstTouch) {   // no-op edit: leave no trace
            m_touched.erase(k);
            if (m_rec) m_rec->forgetLast();
        }
        m_r.m_pendingEdits = !m_touched.empty();
        return changed;
    }
    template<class F> bool editExisting(int tx, int ty, F&& f)
    {
        Tile* t = m_r.m_grid.getWriteTile(tx, ty);
        return t && runEdit(tx, ty, *t, true, f);
    }
    template<class F> bool editOrCreate(int tx, int ty, F&& f)
    {
        Tile* t = m_r.m_grid.getWriteTile(tx, ty);
        const bool created = !t;
        if (created) t = &m_r.m_grid.writeOrCreateTile(tx, ty);
        const bool changed = runEdit(tx, ty, *t, !created, f);   // on throw: the empty tile stays, commit() frees it
        if (!changed && created) m_r.m_grid.freeTile(tx, ty);
        return changed;
    }
    template<class F> bool editTile(int tx, int ty, bool mayCreate, F&& f)
    { return mayCreate ? editOrCreate(tx, ty, f) : editExisting(tx, ty, f); }
};

// Copies `rect` of `src` into `out` (tile-aligned, same coordinates).
inline void lift(TileReader& src, const BoundsI& rect, TileGrid& out)
{
    if (rect.empty()) return;
    auto scratch = std::make_unique<Tile>();
    forEachTileSpan(rect, [&](const TileSpan& d) {
        if (gatherFromTiles(*scratch, src, d, 0, 0))
            out.writeOrCreateTile(d.tx, d.ty).copyFrom(*scratch);
    });
}

// Swaps every entry of `step` with the raster's current tile (undo and redo alike).
[[nodiscard]] inline EditSummary applyStep(RasterData& r, TileDelta& step)
{
    RasterEditor ed(r);   // no recorder: undo itself isn't recorded
    for (TileDelta::Entry& e : step.entries) {
        const auto [tx, ty] = TileUtils::unpackKey(e.key);

        std::unique_ptr<Tile> now;   // what's there right now
        if (const Tile* cur = r.getReadTile(tx, ty)) {
            now = std::make_unique<Tile>();
            now->copyFrom(*cur);
        }
        if (e.state) ed.applyTile(tx, ty, *e.state, { 255, CompositeMode::Replace });
        else         ed.eraseTile(tx, ty);

        e.state = std::move(now);   // the entry now holds the opposite state
    }
    return ed.commit();
}

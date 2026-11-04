#pragma once
// A raster's pixels: its tiles (TileGrid), their bounds, and the one way they change (an edit,
// through a RasterEditor). History keeps snapshots of it (RasterSnapshot): each the raster as it
// was, sharing every tile it didn't change with the raster and with every other snapshot of it.
// Going back to one is restore(): only the tiles that differ are told.
//
//   EditSummary s = raster.edit([&](RasterEditor& ed) { ... });   // what changed
//   auto before = raster.snapshot();                              // costs what changes after it
//   raster.restore(*before);                                      // as it was: s.touched again
//
// Tiles no snapshot near where history is (and not the raster's own) can go cold: kept as their
// encoded bytes, not their pixels (keepWarm, a little at a time).

#include "base/clock.hpp"
#include "raster/tile-grid.hpp"

#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <vector>

// What a committed edit (or a restore) touched: tiles to re-read, and whether the bounds moved.
struct EditSummary {
    TileSet  touched;
    bool     boundsChanged = false;
    uint64_t revision = 0;
};

// === RASTER DATA === //
enum class RasterId : uint32_t {};   // its own type: never mixed up with another number
constexpr RasterId kNoRasterId{ 0xFFFFFFFFu };
class RasterEditor;

// A raster as it was: its tiles (a snapshot of its grid, never written) and their bounds.
struct RasterSnapshot {
    TileGrid tiles;
    BoundsI  bounds;
};

class RasterData {
public:
    explicit RasterData(RasterId id) : m_id(id) {}
    RasterData(const RasterData&) = delete;
    RasterData& operator=(const RasterData&) = delete;
    RasterId id() const { return m_id; }

    // ---- read
    const Tile* getReadTile(int tx, int ty) const { return m_grid.getReadTile(tx, ty); }
    // valid between edits only (an open RasterEditor makes them stale)
    BoundsI getBounds() const
    {
        assert(!m_pendingEdits);
        return m_bounds;
    }
    TileReader reader() const { return m_grid.reader(); }
    template<class F> void forEachTile(F&& f) const { m_grid.forEachRead(std::forward<F>(f)); }
    size_t tileCount() const { assert(!m_pendingEdits); return m_grid.size(); }
    size_t bytes() const { return m_grid.bytes(); }   // the memory its pixels take (history apart)
    // What its snapshots keep beyond its own pixels: tiles only they hold (raw or cold), and the
    // encoded bytes of cold tiles.
    size_t historyBytes() const;
    uint64_t revision() const { return m_revision; }   // bumps on every change

    // ---- the only two ways pixels change
    EditSummary edit(const std::function<void(RasterEditor&)>& body);   // nothing touched: it changed nothing
    EditSummary restore(const RasterSnapshot& s);   // as `s` holds it (one of its own snapshots): what differed

    // ---- history
    std::shared_ptr<const RasterSnapshot> snapshot() const;   // as it is now (between edits)
    // Its own tiles and those of `near` (snapshots where history is now and around it) raw; every
    // other one a snapshot holds cold. A little at a time: false while there's more to do.
    bool keepWarm(std::span<const RasterSnapshot* const> near, const Deadline& until) const;

private:
    friend class RasterEditor;

    RasterId m_id;
    TileGrid m_grid;
    BoundsI  m_bounds;    // of the pixels
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
// An image outside the raster: RGBA8 premultiplied, row by row (width * height * 4 bytes). Made
// canonical as it's gathered into tiles.
struct FlatImage {
    std::span<const uint8_t> rgba;
    int width, height;

    const uint8_t* rowFrom(int x, int y) const { return &rgba[(size_t(y) * size_t(width) + size_t(x)) * sizeof(RGBA)]; }
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

// The image placed at (imageX, imageY), as tiles: canonical, stats settled, empty ones left out.
// Touches only `out`: safe on a worker thread.
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

// out = the source shifted by (shiftX, shiftY), over `destArea`. False (out clear) if nothing contributes.
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
// One edit session on a raster, opened only by the raster itself (recordEdit, flip). Edits are
// tracked per tile; commit() frees tiles that became empty, updates bounds and the revision, and
// reports what changed.
class RasterEditor {
    friend class RasterData;
    explicit RasterEditor(RasterData& r) : m_r(r)
    {
        assert(!r.m_editor && "one RasterEditor per raster at a time");
        r.m_editor = this;
    }

public:
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

    // --- wipe
    void clearAll()
    {
        m_r.m_grid.forEachRead([&](int tx, int ty, const Tile&) { m_touched.insert(TileUtils::packKey(tx, ty)); });
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
        assert(image.rgba.size() == size_t(image.width) * size_t(image.height) * sizeof(RGBA));

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

        // Whole-tile shift: source tiles line up with destination tiles.
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

        // Per side (minX, maxX, minY, maxY): could a touched tile have held the old extreme, and
        // does one still reach it? Shrinking needs a full recompute.
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
        return out;
    }

private:
    RasterData& m_r;
    TileSet     m_touched;
    TileKey     m_lastTouched = kNoKey;   // skips the set lookup for runs of edits in one tile

    template<class F> bool runEdit(int tx, int ty, Tile& t, F&& f)
    {
        const TileKey k = TileUtils::packKey(tx, ty);
        const bool firstTouch = k != m_lastTouched && m_touched.insert(k).second;
        const bool changed = f(t);
        if (changed) m_lastTouched = k;
        else if (firstTouch) m_touched.erase(k);   // no-op edit: leave no trace
        m_r.m_pendingEdits = !m_touched.empty();
        return changed;
    }
    template<class F> bool editExisting(int tx, int ty, F&& f)
    {
        Tile* t = m_r.m_grid.getWriteTile(tx, ty);
        return t && runEdit(tx, ty, *t, f);
    }
    template<class F> bool editOrCreate(int tx, int ty, F&& f)
    {
        Tile* t = m_r.m_grid.getWriteTile(tx, ty);
        const bool created = !t;
        if (created) t = &m_r.m_grid.writeOrCreateTile(tx, ty);
        const bool changed = runEdit(tx, ty, *t, f);   // on throw: the empty tile stays, commit() frees it
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

// ---- RasterData: edits and history

inline EditSummary RasterData::edit(const std::function<void(RasterEditor&)>& body)
{
    RasterEditor ed(*this);
    body(ed);
    return ed.commit();
}

inline std::shared_ptr<const RasterSnapshot> RasterData::snapshot() const
{
    assert(!m_pendingEdits);
    return std::make_shared<const RasterSnapshot>(RasterSnapshot{ m_grid, m_bounds });
}

inline EditSummary RasterData::restore(const RasterSnapshot& s)
{
    assert(!m_editor && m_grid.sharesPoolWith(s.tiles) && "one of its own snapshots, between edits");
    EditSummary out;
    m_grid.forEachDifferent(s.tiles, [&](int tx, int ty) { out.touched.insert(TileUtils::packKey(tx, ty)); });
    if (out.touched.empty()) return out;
    m_grid = s.tiles;
    for (TileKey k : out.touched) {   // its own tiles are raw: the ones it takes from the snapshot decoded now
        const auto [tx, ty] = TileUtils::unpackKey(k);
        (void)m_grid.getReadTile(tx, ty);
    }
    out.boundsChanged = m_bounds != s.bounds;
    m_bounds = s.bounds;
    out.revision = ++m_revision;
    return out;
}

inline size_t RasterData::historyBytes() const
{
    const TilePool& pool = m_grid.pool();
    return pool.rawBytes() - std::min(pool.rawBytes(), m_grid.bytes()) + pool.coldBytes();
}

inline bool RasterData::keepWarm(std::span<const RasterSnapshot* const> near, const Deadline& until) const
{
    TilePool& pool = m_grid.pool();
    absl::flat_hash_set<TileId> warm;
    m_grid.forEachTileId([&](TileId id) { warm.insert(id); });
    for (const RasterSnapshot* s : near) s->tiles.forEachTileId([&](TileId id) { warm.insert(id); });
    bool done = true;
    pool.forEachHeld([&](TileId id) {
        if (!done) return;
        const bool want = warm.contains(id);
        if (want == pool.isRaw(id)) return;
        if (until.expired()) { done = false; return; }
        if (want) pool.warm(id);
        else      pool.cool(id);
    });
    return done;
}

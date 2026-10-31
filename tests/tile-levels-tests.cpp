// Levels of detail kept current as their source changes (TileLevels): whatever was built,
// changed and built again must be exactly what a fresh one makes of the result. That's what
// RasterTiles uploads to the GPU, tile by tile.

#include "node/tile-levels.hpp"

#include <doctest/doctest.h>
#include <algorithm>
#include <map>
#include <random>

namespace {

// Pixels in a grid, its changes told as they're made.
class GridSource : public TileSource {
public:
    TileGrid grid;
    TileSet  changed;

    const Tile* tile(int tx, int ty, RenderCtx&) override { return grid.getReadTile(tx, ty); }
    TileSet footprint() override
    {
        TileSet t;
        grid.forEachRead([&](int tx, int ty, const Tile&) { t.insert(TileUtils::packKey(tx, ty)); });
        return t;
    }
    bool takeChanges(TileSet& out) override
    {
        out = std::exchange(changed, TileSet{});
        return false;
    }
};

using Pixels = std::map<std::pair<int, TileKey>, std::vector<RGBA>>;   // (level, tile) -> what was made

// Every tile of the asked levels built, what's made recorded as an atlas would hold it.
void buildAll(TileLevels& levels, std::initializer_list<int> asked, Pixels& out)
{
    const auto built = [&](int level, TileKey key, const Tile* pixels) {
        if (pixels) out[{ level, key }].assign(pixels->getReadAll(), pixels->getReadAll() + Tile::kPixels);
        else        out.erase({ level, key });
    };
    for (int level : asked)
        for (TileKey key : TileSet(levels.footprint(level)))   // a copy: building a level fills finer footprints
            REQUIRE(levels.build(Deadline::in(Clock::Seconds(60.0)), level, key, built));
}

void paint(GridSource& src, std::mt19937& rng)
{
    std::uniform_int_distribution<int> coord(-200, 4000), shade(0, 255);
    for (int i = 0; i < 40; ++i) {
        const int x = coord(rng), y = coord(rng);
        const uint8_t a = uint8_t(shade(rng));
        Tile& t = src.grid.writeOrCreateTile(TileUtils::worldToTile(x), TileUtils::worldToTile(y));
        t.setPx(TileUtils::worldToLocal(x), TileUtils::worldToLocal(y), canonical(RGBA{ uint8_t(shade(rng)), 40, 90, a }));
        t.settle();
        src.changed.insert(TileUtils::packKey(TileUtils::worldToTile(x), TileUtils::worldToTile(y)));
    }
    if (rng() % 2 && src.grid.size() > 3) {   // a tile wiped out
        std::vector<TileKey> keys;   // in key order: the same tile on every run
        src.grid.forEachRead([&](int tx, int ty, const Tile&) { keys.push_back(TileUtils::packKey(tx, ty)); });
        std::sort(keys.begin(), keys.end());
        const TileKey gone = keys[rng() % keys.size()];
        const auto [tx, ty] = TileUtils::unpackKey(gone);
        src.grid.freeTile(tx, ty);
        src.changed.insert(gone);
    }
}

}

TEST_CASE("levels kept current through changes are what fresh ones make of the result")
{
    for (const uint32_t seed : { 1u, 2u, 3u }) {
        CAPTURE(seed);
        std::mt19937 rng(seed);
        RenderCtx render;
        auto owned = std::make_unique<GridSource>();
        GridSource& src = *owned;
        paint(src, rng);
        src.changed.clear();
        TileLevels kept(std::move(owned), render);
        REQUIRE(kept.sync());   // the first: everything
        Pixels made;
        buildAll(kept, { 0, 3, 7 }, made);   // some levels only: the rest are worked out when asked

        for (int round = 0; round < 6; ++round) {
            CAPTURE(round);
            paint(src, rng);
            CHECK_FALSE(kept.sync());   // changes, not a start over
            buildAll(kept, { 0, 1, 3, 6, 7, 9 }, made);

            auto copy = std::make_unique<GridSource>();
            src.grid.forEachRead([&](int tx, int ty, const Tile& t) { copy->grid.writeOrCreateTile(tx, ty).copyFrom(t); });
            TileLevels fresh(std::move(copy), render);
            REQUIRE(fresh.sync());
            Pixels expected;
            buildAll(fresh, { 0, 1, 3, 6, 7, 9 }, expected);

            Pixels asked;   // what the kept levels made, of the levels compared
            for (const auto& [at, pixels] : made)
                if (at.first == 0 || at.first == 1 || at.first == 3 || at.first == 6 || at.first == 7 || at.first == 9) asked[at] = pixels;
            CHECK(asked.size() == expected.size());
            CHECK(asked == expected);
        }
    }
}

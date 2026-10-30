#include "canvas/tiles/downsample.hpp"

#include <doctest/doctest.h>
#include <vector>

TEST_CASE("downsampling averages premultiplied blocks into their part of the tile")
{
    Tile src;
    {
        Tile::BulkWrite w(src, Tile::BulkWrite::Trusted);
        for (int y = 0; y < Tile::kSize; ++y)
            for (int x = 0; x < Tile::kSize; ++x)
                w.data()[y * Tile::kSize + x] = (x + y) % 2 ? RGBA{ 200, 100, 0, 200 } : RGBA{ 0, 0, 0, 0 };
        w.markChanged();
    }
    std::vector<RGBA> dst(Tile::kPixels, RGBA{ 9, 9, 9, 9 });
    downsampleInto(dst.data(), &src, 1, 1, 0);   // into the right half of the top half
    CHECK(dst[0] == RGBA{ 9, 9, 9, 9 });                                       // the other parts stay
    CHECK(dst[Tile::kSize / 2] == RGBA{ 100, 50, 0, 100 });                    // a checker averages to half
    downsampleInto(dst.data(), nullptr, 3, 0, 0);                               // nothing: transparent
    CHECK(dst[0] == RGBA{ 0, 0, 0, 0 });
    CHECK(dst[Tile::kSize / 8 - 1] == RGBA{ 0, 0, 0, 0 });
    CHECK(dst[Tile::kSize / 8] != RGBA{ 0, 0, 0, 0 });                         // past its part: untouched
}

#include "canvas/tiles/downsample.hpp"

void downsampleInto(RGBA* dst, const Tile* src, int levels, int px, int py)
{
    constexpr int N = Tile::kSize;
    const int block = 1 << levels, sub = N >> levels;   // a block of source pixels per destination pixel
    RGBA* out = dst + size_t(py * sub) * N + px * sub;
    if (!src) {
        for (int y = 0; y < sub; ++y)
            for (int x = 0; x < sub; ++x) out[size_t(y) * N + x] = transparent();
        return;
    }
    const RGBA* s = src->getReadAll();
    if (levels == 1) {   // the pyramid's step: 2x2, unrolled
        for (int y = 0; y < sub; ++y) {
            const RGBA* r0 = s + size_t(2 * y) * N;
            const RGBA* r1 = r0 + N;
            RGBA* d = out + size_t(y) * N;
            for (int x = 0; x < sub; ++x) {
                const RGBA a = r0[2 * x], b = r0[2 * x + 1], c = r1[2 * x], e = r1[2 * x + 1];
                d[x] = { uint8_t((a.r + b.r + c.r + e.r + 2) >> 2), uint8_t((a.g + b.g + c.g + e.g + 2) >> 2),
                         uint8_t((a.b + b.b + c.b + e.b + 2) >> 2), uint8_t((a.a + b.a + c.a + e.a + 2) >> 2) };
            }
        }
        return;
    }
    const int shift = 2 * levels;
    const uint32_t half = (1u << shift) >> 1;
    for (int y = 0; y < sub; ++y)
        for (int x = 0; x < sub; ++x) {
            uint32_t r = 0, g = 0, b = 0, a = 0;
            for (int j = 0; j < block; ++j) {
                const RGBA* row = s + size_t(y * block + j) * N + x * block;
                for (int i = 0; i < block; ++i) { r += row[i].r; g += row[i].g; b += row[i].b; a += row[i].a; }
            }
            out[size_t(y) * N + x] = { uint8_t((r + half) >> shift), uint8_t((g + half) >> shift), uint8_t((b + half) >> shift),
                                       uint8_t((a + half) >> shift) };
        }
}

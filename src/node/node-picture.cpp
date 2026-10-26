#include "node/node-picture.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int kSub     = 2;   // samples per pixel, per axis
constexpr int kChecker = 8;   // checkerboard square, pixels
constexpr uint8_t kCheckerLight = 72, kCheckerDark = 52;

}

void renderNodePicture(Node& n, const BoundsI& area, int width, int height, RGBA* out, int stride, RenderCtx& ctx)
{
    if (area.empty() || width <= 0 || height <= 0) return;
    const float sx = float(area.width()) / float(width), sy = float(area.height()) / float(height);

    // Sample positions per axis (monotonic: each tile owns a contiguous range of them).
    std::vector<int> xs(size_t(width) * kSub), ys(size_t(height) * kSub);
    for (size_t i = 0; i < xs.size(); ++i) xs[i] = area.minX + int(std::floor((float(i) + 0.5f) * sx / kSub));
    for (size_t i = 0; i < ys.size(); ++i) ys[i] = area.minY + int(std::floor((float(i) + 0.5f) * sy / kSub));
    const auto range = [](const std::vector<int>& v, int lo, int hi) {
        return std::make_pair(std::lower_bound(v.begin(), v.end(), lo) - v.begin(),
                              std::upper_bound(v.begin(), v.end(), hi) - v.begin());
    };

    std::vector<uint32_t> accum(size_t(width) * height * 4, 0);
    for (TileKey key : n.footprint()) {
        const auto [tx, ty] = TileUtils::unpackKey(key);
        const int wx = TileUtils::tileToWorld(tx), wy = TileUtils::tileToWorld(ty);
        const auto [x0, x1] = range(xs, wx, wx + Tile::kSize - 1);
        const auto [y0, y1] = range(ys, wy, wy + Tile::kSize - 1);
        if (x0 >= x1 || y0 >= y1) continue;
        const Tile* tile = renderTile(n, tx, ty, ctx);
        if (!tile) continue;
        for (auto y = y0; y < y1; ++y) {
            uint32_t* row = accum.data() + size_t(y / kSub) * width * 4;
            for (auto x = x0; x < x1; ++x) {
                const RGBA p = tile->getReadPx(xs[x] - wx, ys[y] - wy);
                uint32_t* a = row + size_t(x / kSub) * 4;
                a[0] += p.r; a[1] += p.g; a[2] += p.b; a[3] += p.a;
            }
        }
    }

    constexpr uint32_t kDiv = kSub * kSub;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const uint32_t* a = accum.data() + (size_t(y) * width + x) * 4;
            const uint32_t bg = ((x / kChecker + y / kChecker) & 1) ? kCheckerLight : kCheckerDark;
            const uint32_t under = bg * (255 - a[3] / kDiv) / 255;   // premultiplied over the checkerboard
            out[size_t(y) * stride + x] = { uint8_t(std::min<uint32_t>(255, a[0] / kDiv + under)),
                                            uint8_t(std::min<uint32_t>(255, a[1] / kDiv + under)),
                                            uint8_t(std::min<uint32_t>(255, a[2] / kDiv + under)), 255 };
        }
}

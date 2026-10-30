#pragma once
// How pictures of the world are sized, one way for every view: a world rectangle fitted into a
// picture (proportions kept), and the level of detail that matches a scale.
//
//   const PictureFit fit = fitInto(bounds, 256, 192);   // fit.scale, fit.width x fit.height
//   const int level = lodLevel(fit.scale);              // texels at most a pixel each

#include "raster/tile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

constexpr int kLodLevels = 10;   // level L: a texel is 2^L world pixels; level 9 tiles span 32768 pixels

// The level whose texel covers at most one picture pixel at `pixelsPerWorldPixel` (0 zoomed in).
inline int lodLevel(float pixelsPerWorldPixel)
{
    if (pixelsPerWorldPixel >= 1.0f) return 0;
    return std::clamp(int(std::floor(std::log2(1.0f / pixelsPerWorldPixel))), 0, kLodLevels - 1);
}

// The scale that fits `bounds` into width x height, proportions kept.
inline float fitScale(const BoundsI& bounds, float width, float height)
{
    return std::min(width / float(std::max(bounds.width(), 1)), height / float(std::max(bounds.height(), 1)));
}

// ... and the fitted picture in whole pixels (at least one, at most the rectangle).
struct PictureFit {
    float    scale;
    uint32_t width, height;
};
inline PictureFit fitInto(const BoundsI& bounds, uint32_t width, uint32_t height)
{
    const float k = fitScale(bounds, float(width), float(height));
    const auto side = [&](int extent, uint32_t limit) {
        return uint32_t(std::clamp(long(std::lround(float(std::max(extent, 1)) * k)), 1L, long(limit)));
    };
    return { k, side(bounds.width(), width), side(bounds.height(), height) };
}

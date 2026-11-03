#pragma once
// How pictures of the world are sized, one way for every view: a world rectangle fitted into a
// rectangle (proportions kept, centred), and the level of detail that matches a scale. A world
// point p lands at corner + (p - bounds' corner) * scale, the same scale both ways: the graph's
// thumbnails, their previews and the navigator all map the world alike.
//
//   const PictureFit fit = fitInto(bounds, 256, 192);   // fit.width x fit.height at fit.x, fit.y
//   const int level = lodLevel(fit.scale);              // texels at most a pixel each

#include "raster/downsample.hpp"
#include "raster/tile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>


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

// `bounds` in a width x height rectangle (a map in points): where they land, centred.
struct RectFit {
    float scale;           // rectangle units per world pixel
    float x, y;            // the bounds' corner in the rectangle
    float width, height;   // the bounds there
};
inline RectFit fitCentred(const BoundsI& bounds, float width, float height)
{
    const float k = fitScale(bounds, width, height);
    const float w = float(std::max(bounds.width(), 1)) * k, h = float(std::max(bounds.height(), 1)) * k;
    return { k, (width - w) * 0.5f, (height - h) * 0.5f, w, h };
}

// ... as a picture in whole pixels (at least one, at most the rectangle), centred in it. Rounded
// to the nearest pixel, so the last row and column always show the bounds (their centres are
// inside them), never what's past them.
struct PictureFit {
    float    scale;
    uint32_t width, height;   // the picture
    uint32_t x, y;            // its corner in the rectangle
};
inline PictureFit fitInto(const BoundsI& bounds, uint32_t width, uint32_t height)
{
    const RectFit f = fitCentred(bounds, float(width), float(height));
    const auto side = [](float extent, uint32_t limit) { return uint32_t(std::clamp(std::lround(extent), 1L, long(limit))); };
    const uint32_t w = side(f.width, width), h = side(f.height, height);
    return { f.scale, w, h, (width - w) / 2, (height - h) / 2 };
}

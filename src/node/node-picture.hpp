#pragma once
#include "node/node.hpp"

// `n` over world rectangle `area`, scaled into a width x height picture at `out` (rows `stride`
// pixels apart): 2x2 samples per pixel, read only from tiles that exist, averaged over a
// checkerboard. Opaque, straight = premultiplied. Work follows the content, not the area.
void renderNodePicture(Node& n, const BoundsI& area, int width, int height, RGBA* out, int stride, RenderCtx& ctx);

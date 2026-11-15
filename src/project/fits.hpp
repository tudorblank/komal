#pragma once
// The one check per value an edit takes, wherever it comes in: the Workspace's setters,
// previewEdit, the actions' argument ranges and a file being opened (a value that fails is a
// damaged file).

#include "base/overloaded.hpp"
#include "project/artboard.hpp"
#include "node/node.hpp"
#include "raster/tile.hpp"

#include <string>
#include <variant>

// A signal's scale: 0..1 (NaN isn't).
constexpr float kMinScale = 0.0f, kMaxScale = 1.0f;
inline bool scaleFits(float v) { return v >= kMinScale && v <= kMaxScale; }

// A Move's offset: whole pixels inside the world.
inline bool fits(const MoveSettings& m) { return TileUtils::inWorld(m.x) && TileUtils::inWorld(m.y); }

// A node's settings, whichever kind's: each its own rule (a new kind of settings doesn't compile
// without one). A stack's mutes and groups are kept to its rows by the node itself; a file's are
// held to what the node keeps (Workspace::open).
inline bool fits(const NodeSettings& s)
{
    return std::visit(Overloaded{
        [](const NoSettings&) { return true; },
        [](const RasterSettings&) { return true; },
        [](const MoveSettings& m) { return fits(m); },
        [](const CompositorSettings&) { return true; },
    }, s);
}

// A node's or an artboard's name: not empty, and short enough for a .komal file.
constexpr size_t kMaxName = 4096;
inline bool labelFits(const std::string& label) { return !label.empty() && label.size() <= kMaxName; }

// An artboard's rect: not empty, inside the world.
inline bool rectFits(const BoundsI& r)
{
    return !r.empty() && TileUtils::inWorld(r.minX) && TileUtils::inWorld(r.minY) && TileUtils::inWorld(r.maxX) && TileUtils::inWorld(r.maxY);
}
inline bool fits(const Artboard& a) { return labelFits(a.name) && rectFits(a.rect); }

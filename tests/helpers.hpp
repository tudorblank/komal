#pragma once
// What several test files share: a project with a starter graph, and reading its pixels.

#include "base/log.hpp"
#include "document/project.hpp"

#include <doctest/doctest.h>
#include <string>
#include <vector>

// Catches what's logged while it lives (instead of stderr): a test can check a refusal said why.
struct LogCapture {
    LogCapture() { lines().clear(); Log::setSink([](const char* line) { lines().push_back(line); }); }
    ~LogCapture() { Log::setSink(nullptr); }
    static std::vector<std::string>& lines() { static std::vector<std::string> l; return l; }
    bool saw(const char* text) const
    {
        for (const std::string& l : lines()) if (l.find(text) != std::string::npos) return true;
        return false;
    }
};

inline RGBA pixelOf(const Project& p, NodeId raster, int x, int y)
{
    const auto* r = dynamic_cast<const RasterRootNode*>(p.find(raster));
    REQUIRE(r);
    const Tile* t = r->raster().getReadTile(TileUtils::worldToTile(x), TileUtils::worldToTile(y));
    return t ? t->getReadPx(TileUtils::worldToLocal(x), TileUtils::worldToLocal(y)) : transparent();
}

inline void fillPixel(TileGrid& g, int x, int y, RGBA c)
{
    g.writeOrCreateTile(TileUtils::worldToTile(x), TileUtils::worldToTile(y)).setPx(TileUtils::worldToLocal(x), TileUtils::worldToLocal(y), c);
}

// The tests' starter project: a master over two rasters, seeded (no undo steps). "Raster 1" the
// background (a gradient and a half-transparent checkerboard), "Raster 2" a disc across negative
// tile coordinates, active.
struct StarterProject {
    Project project;
    NodeId  master, background, paint;

    StarterProject()
    {
        project.init([&](Project& p) {
            TileGrid bg, disc;
            for (int y = 0; y < 192; ++y)
                for (int x = 0; x < 256; ++x) fillPixel(bg, x, y, { uint8_t(x), uint8_t(y * 255 / 191), 160, 255 });
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 128; ++x)
                    fillPixel(bg, 300 + x, y, ((x / 8) + (y / 8)) % 2 == 0 ? RGBA{ 230, 230, 230, 255 } : RGBA{ 20, 20, 20, 128 });
            for (int y = -96; y <= 0; ++y)
                for (int x = -96; x <= 0; ++x) {
                    const int dx = x + 48, dy = y + 48;
                    if (dx * dx + dy * dy <= 40 * 40) fillPixel(disc, x, y, { 255, 160, 40, 255 });
                }
            background = p.importTiles(bg,   "", { .addToMaster = true, .makeActive = false, .placeNode = true, .nodeX = 40.0f, .nodeY = 0.0f });
            paint      = p.importTiles(disc, "", { .addToMaster = true, .makeActive = true,  .placeNode = true, .nodeX = 40.0f, .nodeY = 370.0f });
        });
        master = project.master().id();
    }
};

// A whole action-like edit: one undo step.
template <class F> void step(Project& p, const char* label, F&& f)
{
    p.beginStep(label);
    f();
    p.endStep();
}

// A new raster holding what `paint` puts in a tile grid (one undo step).
template <class F> NodeId rasterWith(Project& p, F&& paint)
{
    TileGrid tiles;
    paint(tiles);
    NodeId id = kNoNodeId;
    step(p, "Import", [&] { id = p.importTiles(tiles, "test", { .addToMaster = false, .makeActive = false, .placeNode = true, .nodeX = 0, .nodeY = 0 }); });
    return id;
}

// What a node outputs at world pixel (x, y), rendered as the canvas would.
inline RGBA rendered(const Project& p, NodeId id, int x, int y)
{
    Node* n = p.find(id);
    REQUIRE(n);
    RenderCtx ctx;
    const Tile* t = renderTile(*n, TileUtils::worldToTile(x), TileUtils::worldToTile(y), ctx);
    return t ? t->getReadPx(TileUtils::worldToLocal(x), TileUtils::worldToLocal(y)) : transparent();
}

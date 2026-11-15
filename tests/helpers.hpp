#pragma once
// What several test files share: a workspace with a starter graph, reading its pixels, and
// random edits.

#include "base/log.hpp"
#include "node/node-kinds.hpp"
#include "project/workspace.hpp"

#include <doctest/doctest.h>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

// Catches what's logged while it lives (instead of stderr): a test can check what a refusal said.
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

// The pixel at world (x, y) of `tiles`: anything with getReadTile (a raster, a grid).
template <class Tiles> RGBA pixelAt(const Tiles& tiles, int x, int y)
{
    const Tile* t = tiles.getReadTile(TileUtils::worldToTile(x), TileUtils::worldToTile(y));
    return t ? t->getReadPx(TileUtils::worldToLocal(x), TileUtils::worldToLocal(y)) : transparent();
}

inline RGBA pixelOf(const Workspace& p, NodeId raster, int x, int y)
{
    const Node* n = p.find(raster);
    REQUIRE((n && n->type() == NodeType::Raster));
    return pixelAt(static_cast<const RasterRootNode*>(n)->raster(), x, y);
}

inline void fillPixel(TileGrid& g, int x, int y, RGBA c)
{
    g.writeOrCreateTile(TileUtils::worldToTile(x), TileUtils::worldToTile(y)).setPx(TileUtils::worldToLocal(x), TileUtils::worldToLocal(y), c);
}

inline void fillTile(Tile& t, RGBA c)
{
    Tile::BulkWrite w(t, Tile::BulkWrite::Untrusted);
    std::fill(w.data(), w.data() + Tile::kPixels, c);
    w.markChanged();
}

// The tests' starter project: a master over two rasters, seeded (no undo steps). "Raster 1" the
// background (a gradient and a half-transparent checkerboard), "Raster 2" a disc across negative
// tile coordinates, active.
struct StarterWorkspace {
    Workspace workspace;
    NodeId  master, background, paint;

    StarterWorkspace()
    {
        workspace.init([&](Workspace& p) {
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
        master = workspace.master().id();
    }
};

// A whole action-like edit: one undo step.
template <class F> void step(Workspace& p, const char* label, F&& f)
{
    p.beginStep(label);
    f();
    p.endStep();
}

// A new raster holding what `paint` puts in a tile grid (one undo step).
template <class F> NodeId rasterWith(Workspace& p, F&& paint)
{
    TileGrid tiles;
    paint(tiles);
    NodeId id = kNoNodeId;
    step(p, "Import", [&] { id = p.importTiles(tiles, "test", { .addToMaster = false, .makeActive = false, .placeNode = true, .nodeX = 0, .nodeY = 0 }); });
    return id;
}

// What a node outputs at world pixel (x, y), rendered as the canvas would.
inline RGBA rendered(const Workspace& p, NodeId id, int x, int y)
{
    Node* n = p.find(id);
    REQUIRE(n);
    struct Render {
        Node& node;
        mutable RenderCtx ctx;
        const Tile* getReadTile(int tx, int ty) const { return renderTile(node, tx, ty, ctx); }
    };
    return pixelAt(Render{ *n, {} }, x, y);
}

// Random picks for the tests that grow graphs at random, and the edits they share (each one undo
// step; it may change nothing).
class RandomEdits {
public:
    explicit RandomEdits(uint32_t seed) : m_rng(seed) {}

    int    pick(int n) { return std::uniform_int_distribution<int>(0, n - 1)(m_rng); }
    float  unit() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng); }
    NodeId anyNode(const Workspace& p) { return p.nodes()[size_t(pick(int(p.nodes().size())))]->id(); }

    // A node of any kind at (x, y), fed from any node `fed` of the time.
    void addNode(Workspace& p, float x, float y, float fed)
    {
        const NodeType type = nodeKinds()[size_t(pick(int(nodeKinds().size())))].type;   // every kind, a new one too
        step(p, "Add", [&] { p.addNode(type, x, y, { .from = unit() < fed ? anyNode(p) : kNoNodeId, .into = kNoNodeId, .slot = {} }); });
    }

    // A link from any node into any node, if it can be: its input, a new layer, a mask, or (`rows`)
    // a layer's row.
    void connect(Workspace& p, bool rows)
    {
        const NodeId from = anyNode(p), to = anyNode(p);
        std::vector<InputSlot> slots{ InputSlot::input(), InputSlot::newLayer(uint32_t(pick(3))) };
        if (rows) slots.push_back(InputSlot::stackLayer(uint32_t(pick(3))));
        slots.push_back(InputSlot::mask(NodeSignal(pick(3)), NodeChannel(pick(4))));
        const InputSlot slot = slots[size_t(pick(int(slots.size())))];
        if (p.canConnect(from, to, slot)) step(p, "Connect", [&] { p.connect(from, to, slot); });
    }

private:
    std::mt19937 m_rng;
};

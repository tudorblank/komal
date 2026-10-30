#pragma once
// What a RasterTiles reads: level 0 tiles from somewhere, which of them may hold something, and
// which changed since it last asked. One kind per kind of pixels:
//
//   NodeOutputSource   what a node shows (its signals applied): the canvas, thumbnails, previews
//   NodeWetSource      a node's raw wet (a raster's own pixels): what a preview reshapes
//   (the stroke's coverage: canvas/preview/stroke-source.hpp)

#include "node/node.hpp"

#include <memory>

class TileSource {
public:
    virtual ~TileSource() = default;
    // Tile (tx, ty), or null if empty; valid until the next call.
    virtual const Tile* tile(int tx, int ty, RenderCtx& ctx) = 0;
    virtual TileSet footprint() = 0;   // the tiles that may hold something
    // Drains what changed since the last call: true when everything did (and `changed` is empty).
    virtual bool takeChanges(TileSet& changed) = 0;
};

class NodeOutputSource : public TileSource {
public:
    explicit NodeOutputSource(NodePtr node) : m_node(std::move(node)), m_watch(m_node->watchOutput()) {}
    const Tile* tile(int tx, int ty, RenderCtx& ctx) override { return renderTile(*m_node, tx, ty, ctx); }
    TileSet footprint() override { return m_node->footprint(); }
    bool takeChanges(TileSet& changed) override { return m_watch->take(changed); }

private:
    NodePtr m_node;
    std::shared_ptr<TileWatch> m_watch;
};

// Changes follow the node's output: anything that changes the wet changes it too (and a signal
// change re-reads tiles that didn't change, which costs only time).
class NodeWetSource : public TileSource {
public:
    explicit NodeWetSource(NodePtr node) : m_node(std::move(node)), m_watch(m_node->watchOutput()) {}
    const Tile* tile(int tx, int ty, RenderCtx& ctx) override
    {
        ctx.beginTile();
        return m_node->fetchWet(tx, ty, ctx);
    }
    TileSet footprint() override
    {
        TileSet t;
        m_node->wetFootprint(t);
        return t;
    }
    bool takeChanges(TileSet& changed) override { return m_watch->take(changed); }

private:
    NodePtr m_node;
    std::shared_ptr<TileWatch> m_watch;
};

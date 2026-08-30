#pragma once
#include "raster.hpp"

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>

inline RGBA transparent() { return {0, 0, 0, 0}; }
inline RGBA applyOpacityFill(RGBA c, float fill, float opacity)
{
    if(fill < 1.0f)    c.a = (uint8_t)(c.a * fill + 0.5f);
    if(opacity < 1.0f) c.a = (uint8_t)(c.a * opacity + 0.5f);
    return c;
}

struct TileRange { int minTX, maxTX, minTY, maxTY; bool valid; };

// ==== BASE NODE CLASS ====
class Node : public std::enable_shared_from_this<Node>{
public:
    virtual ~Node() {}

    virtual RGBA computePixel(int worldX, int worldY) = 0;
    virtual BoundsI computeBounds() = 0;
    virtual Chunk* readSourceChunk(int /*tileX*/, int /*tileY*/) { return nullptr; } // RasterRootNode only
    virtual void collectOccupiedTiles(std::unordered_set<uint64_t>& out) // set of tile coords
    {
        // fallback for nodes that don't track real occupancy: whole bounding rect
        BoundsI b = computeBounds();
        TileRange r = boundsToTileRange(b);
        if(!r.valid) return;
        for(int ty = r.minTY; ty <= r.maxTY; ty++)
            for(int tx = r.minTX; tx <= r.maxTX; tx++)
                out.insert(Key::pack(tx, ty));
    }

    // opacity
    float m_opacity = 1.0f;
    float m_fill = 1.0f;
    RGBA sampleBlended(int worldX, int worldY)
    {
        RGBA c = computePixel(worldX, worldY);
        return applyOpacityFill(c, m_fill, m_opacity);
    }

    // caching
    std::vector<std::weak_ptr<Node>> m_listeners;
    static void listenTo(const std::shared_ptr<Node>& input, const std::shared_ptr<Node>& listener)
    { if(input) input->m_listeners.push_back(listener); }
    virtual void invalidateNode()
    {
        std::erase_if(m_listeners, [](const std::weak_ptr<Node>& w){ return w.expired(); });
        for(auto& weak : m_listeners)
            if(auto listener = weak.lock())
                listener->invalidateNode();
    }
    virtual void invalidateTile(int tileX, int tileY)
    { propagateInvalidateTile(tileX, tileY); }

protected:
    void propagateInvalidateTile(int tileX, int tileY)
    {
        std::erase_if(m_listeners, [](const std::weak_ptr<Node>& w){ return w.expired(); });
        for(auto& weak : m_listeners)
            if(auto listener = weak.lock())
                listener->invalidateTile(tileX, tileY);
    }
    static TileRange boundsToTileRange(const BoundsI& b)
    {
        if(!b.valid) return TileRange{0,0,0,0,false};
        return TileRange{
            Grid::worldToChunk(b.minX), Grid::worldToChunk(b.maxX),
            Grid::worldToChunk(b.minY), Grid::worldToChunk(b.maxY),
            true
        };
    }
};

// ==== RASTER ROOT NODE ====
class RasterRootNode : public Node{
private:
    struct Private { explicit Private() = default; };

public:
    // init
    RasterRootNode(Private, RasterData* sampledRaster) : m_inputRaster(sampledRaster) {}
    static std::shared_ptr<RasterRootNode> create(RasterData* raster)
    { return std::make_shared<RasterRootNode>(Private{}, raster); }

    RasterData* m_inputRaster;

    // compute
    Chunk* readSourceChunk(int tileX, int tileY) override
    { return m_inputRaster ? m_inputRaster->readChunk(tileX, tileY) : nullptr; }
    RGBA computePixel(int worldX, int worldY) override
    {
        if(!m_inputRaster) return transparent();

        int cx = Grid::worldToChunk(worldX);
        int cy = Grid::worldToChunk(worldY);

        Chunk* readChunk = readSourceChunk(cx, cy);
        if(!readChunk) return transparent();

        return readChunk->pixel(Grid::worldToChunkLocal(worldX), Grid::worldToChunkLocal(worldY));
    }
    BoundsI computeBounds() override
    {
        if(!m_inputRaster)
        { BoundsI b; b.reset(); return b; }
        return m_inputRaster->getPixelBounds();
    }
    void collectOccupiedTiles(std::unordered_set<uint64_t>& out) override
    {
        if(!m_inputRaster) return;
        for(Key::XY pos : m_inputRaster->m_grid.listOccupiedChunks())
            out.insert(Key::pack(pos.x, pos.y));
    }

    // pixel editing
    void paintPixel(int worldX, int worldY, RGBA color)
    {
        if(m_inputRaster) m_inputRaster->setPixel(worldX, worldY, color);
        invalidateTile(Grid::worldToChunk(worldX), Grid::worldToChunk(worldY));
    }
    void erasePixel(int worldX, int worldY)
    {
        if(m_inputRaster) m_inputRaster->erasePixel(worldX, worldY);
        invalidateTile(Grid::worldToChunk(worldX), Grid::worldToChunk(worldY));
    }
};

// ==== CHANNEL SPLIT NODE ====
enum class Channel { R, G, B, A };
class ChannelSplitNode : public Node{
private:
    struct Private { explicit Private() = default; };

public:
    // init
    ChannelSplitNode(Private, std::shared_ptr<Node> input, Channel channel)
        : m_inputNode(std::move(input)), m_channel(channel) {}
    static std::shared_ptr<ChannelSplitNode> create(std::shared_ptr<Node> input, Channel channel)
    {
        auto node = std::make_shared<ChannelSplitNode>(Private{}, input, channel);
        listenTo(input, node);
        return node;
    }

    std::shared_ptr<Node> m_inputNode;
    Channel m_channel;

    // compute
    RGBA computePixel(int worldX, int worldY) override
    {
        if(!m_inputNode) return transparent();
        RGBA c = m_inputNode->sampleBlended(worldX, worldY);
        uint8_t v = (m_channel == Channel::R) ? c.r : (m_channel == Channel::G) ? c.g : (m_channel == Channel::B) ? c.b : c.a;
        return { v, v, v, 255 };
    }
    BoundsI computeBounds() override
    { return m_inputNode ? m_inputNode->computeBounds() : BoundsI{}; }
};

// ==== MOVE NODE ====
class MoveNode : public Node{
private:
    struct Private { explicit Private() = default; };

public:
    // init
    MoveNode(Private, std::shared_ptr<Node> input, int offsetX, int offsetY)
        : m_inputNode(std::move(input)), m_offsetX(offsetX), m_offsetY(offsetY) {}
    static std::shared_ptr<MoveNode> create(std::shared_ptr<Node> input, int offsetX = 0, int offsetY = 0)
    {
        auto node = std::make_shared<MoveNode>(Private{}, input, offsetX, offsetY);
        listenTo(input, node);
        return node;
    }

    // functionality
    std::shared_ptr<Node> m_inputNode;
    int m_offsetX = 0;
    int m_offsetY = 0;

    std::unordered_set<uint64_t> setOffset(int offsetX, int offsetY)
    {
        if(offsetX == m_offsetX && offsetY == m_offsetY) return {};

        std::unordered_set<uint64_t> oldTiles;
        collectOccupiedTiles(oldTiles); // uses current (old) offset

        m_offsetX = offsetX;
        m_offsetY = offsetY;

        std::unordered_set<uint64_t> newTiles;
        collectOccupiedTiles(newTiles); // uses new offset

        for(uint64_t key : oldTiles) { Key::XY p = Key::unpack(key); propagateInvalidateTile(p.x, p.y); }
        for(uint64_t key : newTiles) { Key::XY p = Key::unpack(key); propagateInvalidateTile(p.x, p.y); }

        oldTiles.insert(newTiles.begin(), newTiles.end());
        return oldTiles;
    }
    void reset() { setOffset(0, 0); }

    // compute
    RGBA computePixel(int worldX, int worldY) override
    {
        if(!m_inputNode) return transparent();
        return m_inputNode->sampleBlended(worldX - m_offsetX, worldY - m_offsetY);
    }
    BoundsI computeBounds() override
    {
        if(!m_inputNode)
        { BoundsI b; b.reset(); return b; }

        BoundsI b = m_inputNode->computeBounds();
        if(!b.valid) return b;

        b.minX += m_offsetX; b.maxX += m_offsetX;
        b.minY += m_offsetY; b.maxY += m_offsetY;
        return b;
    }

    // caching
    void collectOccupiedTiles(std::unordered_set<uint64_t>& out) override
    {
        if(!m_inputNode) return;

        std::unordered_set<uint64_t> rawTiles;
        m_inputNode->collectOccupiedTiles(rawTiles);

        for(uint64_t key : rawTiles)
        {
            Key::XY t = Key::unpack(key);

            int worldMinX = t.x * Grid::CHUNK_SIZE + m_offsetX;
            int worldMinY = t.y * Grid::CHUNK_SIZE + m_offsetY;
            int worldMaxX = worldMinX + Grid::CHUNK_SIZE - 1;
            int worldMaxY = worldMinY + Grid::CHUNK_SIZE - 1;

            int dstMinTX = Grid::worldToChunk(worldMinX), dstMaxTX = Grid::worldToChunk(worldMaxX);
            int dstMinTY = Grid::worldToChunk(worldMinY), dstMaxTY = Grid::worldToChunk(worldMaxY);

            for(int ty = dstMinTY; ty <= dstMaxTY; ty++)
                for(int tx = dstMinTX; tx <= dstMaxTX; tx++)
                    out.insert(Key::pack(tx, ty));
        }
    }
    void invalidateTile(int tileX, int tileY) override
    {
        if(!m_inputNode) { propagateInvalidateTile(tileX, tileY); return; }

        int worldMinX = tileX * Grid::CHUNK_SIZE + m_offsetX;
        int worldMinY = tileY * Grid::CHUNK_SIZE + m_offsetY;
        int worldMaxX = worldMinX + Grid::CHUNK_SIZE - 1;
        int worldMaxY = worldMinY + Grid::CHUNK_SIZE - 1;

        int dstMinTX = Grid::worldToChunk(worldMinX), dstMaxTX = Grid::worldToChunk(worldMaxX);
        int dstMinTY = Grid::worldToChunk(worldMinY), dstMaxTY = Grid::worldToChunk(worldMaxY);

        for(int ty = dstMinTY; ty <= dstMaxTY; ty++)
            for(int tx = dstMinTX; tx <= dstMaxTX; tx++)
                propagateInvalidateTile(tx, ty); // may fire 1-4 times depending on offset alignment
    }
};
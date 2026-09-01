#pragma once

#include "raster/raster-utils.hpp"
#include "raster/raster-base.hpp"

#include "gfx/blursys.hpp"

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <cstring>
#include <algorithm>

#include <QString>

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

    struct Meta{
        float x, y;
        QString label;
    };
    Meta m_meta;

    virtual RGBA computePixel(int worldX, int worldY) = 0;
    virtual BoundsI computeBounds() = 0;
    virtual Chunk* readSourceChunk(int /*tileX*/, int /*tileY*/) { return nullptr; } // RasterRootNode only
    virtual void collectOccupiedTiles(std::unordered_set<uint64_t>& out) // set of tile coords
    {
        // fallback for nodes that don't track real occupancy
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

// ==== BLUR NODE ====
class BlurNode : public Node{
private:
    struct Private { explicit Private() = default; };

public:
    BlurNode(Private, std::shared_ptr<Node> input, int radius)
        : m_inputNode(std::move(input)), m_radius(radius) {}
    static std::shared_ptr<BlurNode> create(std::shared_ptr<Node> input, int radius)
    {
        auto node = std::make_shared<BlurNode>(Private{}, input, radius);
        listenTo(input, node);
        return node;
    }

    std::shared_ptr<Node> m_inputNode;
    int m_radius;

    BlurSys* m_blurSys = nullptr;
    void setBlurSys(BlurSys* sys) { m_blurSys = sys; }

    SparseRasterGrid m_cache; // one blurred Chunk per tile

    BoundsI computeBounds() override
    {
        if(!m_inputNode) { BoundsI b; b.reset(); return b; }
        BoundsI b = m_inputNode->computeBounds();
        if(!b.valid) return b;
        b.minX -= m_radius; b.maxX += m_radius;
        b.minY -= m_radius; b.maxY += m_radius;
        return b;
    }

    RGBA computePixel(int worldX, int worldY) override
    {
        Chunk& c = getOrBuildTile(Grid::worldToChunk(worldX), Grid::worldToChunk(worldY));
        return c.pixel(Grid::worldToChunkLocal(worldX), Grid::worldToChunkLocal(worldY));
    }
    Chunk* readSourceChunk(int tileX, int tileY) override
    { return &getOrBuildTile(tileX, tileY); } // lets the compositor take the fast copy path

    void invalidateTile(int tileX, int tileY) override
    {
        int reach = (m_radius + Grid::CHUNK_SIZE - 1) / Grid::CHUNK_SIZE; // ceil(R / CHUNK_SIZE)
        for(int dy = -reach; dy <= reach; dy++)
            for(int dx = -reach; dx <= reach; dx++)
                m_cache.freeChunk(tileX + dx, tileY + dy);
        propagateInvalidateTile(tileX, tileY);
    }

private:
    Chunk& getOrBuildTile(int tileX, int tileY)
    {
        Chunk* existing = m_cache.readChunk(tileX, tileY);
        if(existing) return *existing;

        Chunk& out = m_cache.accessOrCreateChunk(tileX, tileY);
        gpuBlurTile(tileX, tileY, out);
        return out;
    }
    void gpuBlurTile(int tileX, int tileY, Chunk& out)
    {
        if(!m_inputNode || !m_blurSys)
        {
            for(RGBA& px : out.data) px = transparent();
            return;
        }

        int R = m_radius;
        int paddedSize = Grid::CHUNK_SIZE + 2 * R;
        std::vector<RGBA> padded(paddedSize * paddedSize, transparent());

        // world-space bounds of the padded region
        int worldX0 = tileX * Grid::CHUNK_SIZE - R;
        int worldY0 = tileY * Grid::CHUNK_SIZE - R;
        int worldX1 = worldX0 + paddedSize - 1;
        int worldY1 = worldY0 + paddedSize - 1;

        // which input chunks overlap that region
        int minCX = Grid::worldToChunk(worldX0), maxCX = Grid::worldToChunk(worldX1);
        int minCY = Grid::worldToChunk(worldY0), maxCY = Grid::worldToChunk(worldY1);

        for(int cy = minCY; cy <= maxCY; cy++)
        for(int cx = minCX; cx <= maxCX; cx++)
        {
            // world-space bounds of this input chunk
            int chunkWorldX0 = cx * Grid::CHUNK_SIZE;
            int chunkWorldY0 = cy * Grid::CHUNK_SIZE;
            int chunkWorldX1 = chunkWorldX0 + Grid::CHUNK_SIZE - 1;
            int chunkWorldY1 = chunkWorldY0 + Grid::CHUNK_SIZE - 1;

            // overlap between chunk and the padded region, in world space
            int ovMinX = std::max(worldX0, chunkWorldX0), ovMaxX = std::min(worldX1, chunkWorldX1);
            int ovMinY = std::max(worldY0, chunkWorldY0), ovMaxY = std::min(worldY1, chunkWorldY1);
            if(ovMinX > ovMaxX || ovMinY > ovMaxY) continue; // shouldn't happen given the range calc, but safe

            Chunk* srcChunk = m_inputNode->readSourceChunk(cx, cy);
            int rowLen = ovMaxX - ovMinX + 1;

            if(srcChunk)
            {
                // fast path (source chunk)
                for(int wy = ovMinY; wy <= ovMaxY; wy++)
                {
                    int ly = wy - chunkWorldY0;
                    int lx0 = ovMinX - chunkWorldX0;
                    int py = wy - worldY0;
                    int px0 = ovMinX - worldX0;

                    memcpy(&padded[py * paddedSize + px0],
                        &srcChunk->data[ly * Grid::CHUNK_SIZE + lx0],
                        rowLen * sizeof(RGBA));
                }
            }
            else
            {
                // slow path (scoped only to this chunk's overlap)
                for(int wy = ovMinY; wy <= ovMaxY; wy++)
                    for(int wx = ovMinX; wx <= ovMaxX; wx++)
                    {
                        int py = wy - worldY0, px = wx - worldX0;
                        padded[py * paddedSize + px] = m_inputNode->sampleBlended(wx, wy);
                    }
            }
        }

        m_blurSys->blurTile(padded.data(), paddedSize, R, out.data);

        out.m_pixelCount = 0;
        for(const RGBA& px : out.data)
            if(px.a > 0) out.m_pixelCount++;
        out.localBounds.dirty = true;
    }
};
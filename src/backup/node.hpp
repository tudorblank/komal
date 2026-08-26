#pragma once
#include "raster.hpp"

#include <vector>
#include <unordered_map>
#include <memory>

class CompositorNode;

inline RGBA transparent() { return {0, 0, 0, 0}; }

class NodeCache{
public:
    ChunkPool m_chunkPool;
    std::unordered_map<uint64_t, uint32_t> tileIndexMap; // key, tile index

    Chunk& getOrCreateTile(CompositorNode& source, int tileX, int tileY);
    RGBA sample(CompositorNode& source, int worldX, int worldY);
    
    void invalidateAll()
    {
        for(auto& [key, idx] : tileIndexMap)
            m_chunkPool.freeIndex(idx);
        tileIndexMap.clear();
    }
    void invalidateTile(int tx, int ty)
    {
        uint64_t key = Key::pack(tx, ty);
        auto it = tileIndexMap.find(key);
        if(it == tileIndexMap.end()) return;

        m_chunkPool.freeIndex(it->second);
        tileIndexMap.erase(it);
    }
};

// ==== BASE NODE CLASS ====
class Node : public std::enable_shared_from_this<Node>{
public:
    virtual ~Node() {}

    virtual RGBA computePixel(int worldX, int worldY) = 0;
    virtual BoundsI computeBounds() = 0;

    float m_opacity = 1.0f;
    float m_fill = 1.0f;
    RGBA sample(int worldX, int worldY) // for opacity / fill
    {
        RGBA c = computePixel(worldX, worldY);
        if(m_fill < 1.0f)
            c.a = (uint8_t)(c.a * m_fill + 0.5f);
        if(m_opacity < 1.0f)
            c.a = (uint8_t)(c.a * m_opacity + 0.5f);
        return c;
    }

    // caching
    std::vector<std::weak_ptr<Node>> m_listeners;
    static void listenTo(const std::shared_ptr<Node>& input, const std::shared_ptr<Node>& listener)
    { if(input) input->m_listeners.push_back(listener); }

    bool m_gpuBakeDirty = true;
    bool needsGPUBake() const { return m_gpuBakeDirty; }
    void markGPUBaked()       { m_gpuBakeDirty = false; }

    virtual void invalidate()
    {
        m_gpuBakeDirty = true;
        std::erase_if(m_listeners, [](const std::weak_ptr<Node>& w){ return w.expired(); });
        for(auto& weak : m_listeners)
            if(auto listener = weak.lock())
                listener->invalidate();
    }
    virtual void invalidateTile(int tileX, int tileY)
    {
        std::erase_if(m_listeners, [](const std::weak_ptr<Node>& w){ return w.expired(); });
        for(auto& weak : m_listeners)
            if(auto listener = weak.lock())
                listener->invalidateTile(tileX, tileY);
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
    RGBA computePixel(int worldX, int worldY) override
    {
        if(!m_inputRaster) return transparent();

        int cx = Grid::worldToChunk(worldX);
        int cy = Grid::worldToChunk(worldY);

        Chunk* chunk = m_inputRaster->readChunk(cx, cy); // read only
        if(!chunk) return transparent();

        return chunk->pixel(Grid::worldToLocal(worldX), Grid::worldToLocal(worldY));
    }
    BoundsI computeBounds() override
    {
        if(!m_inputRaster)
        { BoundsI b; b.reset(); return b; }
        return m_inputRaster->getPixelBounds();
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

// ==== COMPOSITOR NODE ====
class CompositorNode : public Node{
private:
    struct Private { explicit Private() = default; };

    static uint8_t clamp255(int v)
    { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

    static uint8_t mixChannel(uint8_t s, uint8_t d, float srcA, float dstA, float outA)
    {
        float out = (s * srcA + d * dstA * (1.0f - srcA)) / outA;
        return clamp255((int)(out + 0.5f));
    }

    // standard "src over dst", non-premultiplied
    static RGBA over(RGBA dst, RGBA src)
    {
        if(src.a == 0) return dst;
        if(src.a == 255) return src;

        float srcA = src.a / 255.0f;
        float dstA = dst.a / 255.0f;
        float outA = srcA + dstA * (1.0f - srcA);
        if(outA <= 0.0f) return transparent();

        return {
            mixChannel(src.r, dst.r, srcA, dstA, outA),
            mixChannel(src.g, dst.g, srcA, dstA, outA),
            mixChannel(src.b, dst.b, srcA, dstA, outA),
            clamp255((int)(outA * 255.0f + 0.5f))
        };
    }

public:
    // init
    explicit CompositorNode(Private) {}
    static std::shared_ptr<CompositorNode> create()
    { return std::make_shared<CompositorNode>(Private{}); }

    // layer stack
    std::vector<std::shared_ptr<Node>> m_layers;
    void addLayer(std::shared_ptr<Node> input)
    {
        listenTo(input, shared_from_this());
        m_layers.push_back(std::move(input));
        invalidate();
    }
    void removeLayer(size_t index)
    {
        if(index >= m_layers.size()) return;
        m_layers.erase(m_layers.begin() + index);
        invalidate();
    }
    void moveLayer(size_t fromIndex, size_t toIndex)
    {
        if(fromIndex >= m_layers.size() || toIndex >= m_layers.size() || fromIndex == toIndex) return;
        auto moved = std::move(m_layers[fromIndex]);
        m_layers.erase(m_layers.begin() + fromIndex);
        m_layers.insert(m_layers.begin() + toIndex, std::move(moved));
        invalidate();
    }
    size_t layerCount() const { return m_layers.size(); }

    // compute
    RGBA computePixel(int worldX, int worldY) override
    {
        if(m_cache) return m_cache->sample(*this, worldX, worldY);
        return computeRaw(worldX, worldY);
    }
    RGBA computeRaw(int worldX, int worldY)
    {
        RGBA result = transparent();
        for(auto& layer : m_layers)
        {
            if(!layer) continue;
            result = over(result, layer->sample(worldX, worldY));
        }
        return result;
    }
    BoundsI computeBounds() override
    {
        BoundsI result;
        result.reset();
        for(auto& layer : m_layers)
        {
            if(!layer) continue;
            BoundsI b = layer->computeBounds();
            if(b.valid)
            {
                result.expand(b.minX, b.minY);
                result.expand(b.maxX, b.maxY);
            }
        }
        return result;
    }

    // caching //
    std::unique_ptr<NodeCache> m_cache;
    bool m_isMain = false;

    void setMain(bool isMain)
    {
        m_isMain = isMain;
        if(isMain && !m_cache) m_cache = std::make_unique<NodeCache>();
        if(!isMain) m_cache.reset();
    }
    void invalidate() override
    {
        if(m_cache) m_cache->invalidateAll();
        Node::invalidate();
    }
    void invalidateTile(int tx, int ty) override
    {
        if(m_cache) m_cache->invalidateTile(tx, ty);
        Node::invalidateTile(tx, ty);
    }

    const Chunk& getTileBuffer(int tileX, int tileY)
    {
        if(!m_cache) setMain(true);
        return m_cache->getOrCreateTile(*this, tileX, tileY);
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
        RGBA c = m_inputNode->sample(worldX, worldY);
        uint8_t v = (m_channel == Channel::R) ? c.r : (m_channel == Channel::G) ? c.g : (m_channel == Channel::B) ? c.b : c.a;
        return { v, v, v, 255 };
    }
    BoundsI computeBounds() override
    { return m_inputNode ? m_inputNode->computeBounds() : BoundsI{}; }
};
#pragma once
#include "raster.hpp"

#include <vector>
#include <unordered_map>
#include <memory>

inline RGBA transparent() { return {0, 0, 0, 0}; }
inline RGBA applyOpacityFill(RGBA c, float fill, float opacity)
{
    if(fill < 1.0f)    c.a = (uint8_t)(c.a * fill + 0.5f);
    if(opacity < 1.0f) c.a = (uint8_t)(c.a * opacity + 0.5f);
    return c;
}

// ==== BASE NODE CLASS ====
class Node : public std::enable_shared_from_this<Node>{
public:
    virtual ~Node() {}

    virtual RGBA computePixel(int worldX, int worldY) = 0;
    virtual BoundsI computeBounds() = 0;
    virtual Chunk* readSourceChunk(int /*tileX*/, int /*tileY*/) { return nullptr; } // RasterRootNode only

    // opacity
    float m_opacity = 1.0f;
    float m_fill = 1.0f;
    RGBA sampleBlended(int worldX, int worldY)
    {
        RGBA c = computePixel(worldX, worldY);
        return applyOpacityFill(c, m_fill, m_opacity);
    }

    // gpu bake
    bool m_gpuBakeDirty = true;
    bool needsGPUBake() const { return m_gpuBakeDirty; }
    void markGPUBaked()       { m_gpuBakeDirty = false; }

    // caching
    std::vector<std::weak_ptr<Node>> m_listeners;
    static void listenTo(const std::shared_ptr<Node>& input, const std::shared_ptr<Node>& listener)
    { if(input) input->m_listeners.push_back(listener); }

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
    Chunk* readSourceChunk(int tileX, int tileY) override
    { return m_inputRaster ? m_inputRaster->readChunk(tileX, tileY) : nullptr; }
    
    // compute
    RGBA computePixel(int worldX, int worldY) override
    {
        if(!m_inputRaster) return transparent();

        int cx = Grid::worldToChunk(worldX);
        int cy = Grid::worldToChunk(worldY);

        Chunk* readChunk = readSourceChunk(cx, cy);
        if(!readChunk) return transparent();

        return readChunk->pixel(Grid::worldToLocal(worldX), Grid::worldToLocal(worldY));
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
#pragma once
#include "raster.hpp"

#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <algorithm>
#include <cmath>

// base node class
class Node : public std::enable_shared_from_this<Node>
{
public:
    virtual ~Node() {}

    // public contract
    RGBA samplePixel(int worldX, int worldY);
    BoundsI bounds();

    void invalidate();
    void invalidateTile(int tileX, int tileY);
    const std::vector<RGBA>& getTile(int tileX, int tileY);

    // GPU bake tracking
    bool needsGPUBake() const { return m_gpuBakeDirty; }
    void markGPUBaked()       { m_gpuBakeDirty = false; }

    static inline RGBA transparent() { return {0,0,0,0}; }

protected:
    // IMPLEMENTED IN OTHER NODE TYPES
    virtual RGBA computePixel(int worldX, int worldY) = 0;
    virtual BoundsI computeBounds() = 0;

    static void listenTo(const std::shared_ptr<Node>& input, const std::shared_ptr<Node>& listener)
    {
        if(input) input->m_listeners.push_back(listener);
    }

    bool m_useCache = false;

private:
    void refreshTile(int tileX, int tileY);

    std::vector<std::weak_ptr<Node>> m_listeners;

    uint64_t m_generation = 1; // start at 1

    // bounds cache (memoized per-generation)
    uint64_t m_boundsGeneration = 0;
    BoundsI m_boundsCache; // valid when m_boundsGeneration == m_generation

    // tiled pixel cache
    static constexpr int TILE_SIZE = 64;
    std::unordered_map<uint64_t, std::vector<RGBA>> m_tileCache; // accessed by tileKey

    bool m_gpuBakeDirty = true;

#ifdef NODE_DEBUG_INSPECT
    // Debug inspector needs access to internals
    friend struct NodeInspector;
#endif
};

// --- RASTER ROOT ---
class RasterRootNode : public Node
{
public:
    static std::shared_ptr<RasterRootNode> create(RasterData* raster)
    { return std::shared_ptr<RasterRootNode>(new RasterRootNode(raster)); }

    RasterData* raster() const { return m_raster; }

    // source raster editing pass-throughs
    void paintPixel(int worldX, int worldY, RGBA color)
    {
        if(!m_raster) return;
        m_raster->setPixel(worldX, worldY, color);
        invalidateTile(RasterData::worldToChunk(worldX), RasterData::worldToChunk(worldY));
    }
    void erasePixel(int worldX, int worldY)
    {
        if(!m_raster) return;
        m_raster->erasePixel(worldX, worldY);
        invalidateTile(RasterData::worldToChunk(worldX), RasterData::worldToChunk(worldY));
    }

protected:
    RGBA computePixel(int worldX, int worldY) override;
    BoundsI computeBounds() override;

private:
    explicit RasterRootNode(RasterData* raster) : m_raster(raster) { m_useCache = false; }
    RasterData* m_raster;
};

// --- COMPOSITOR ---
enum class BlendMode { Normal, Multiply, Add, Screen };

struct CompositorLayer
{
    std::shared_ptr<Node> input;
    float opacity = 1.0f;
    BlendMode blend = BlendMode::Normal;
    bool visible = true;
};

class CompositorNode : public Node
{
public:
    static std::shared_ptr<CompositorNode> create()
    {
        return std::shared_ptr<CompositorNode>(new CompositorNode());
    }

    void addLayer(std::shared_ptr<Node> input, float opacity = 1.0f,
                  BlendMode blend = BlendMode::Normal, bool visible = true);

    void setLayerOpacity(size_t index, float opacity);
    void setLayerBlend(size_t index, BlendMode blend);
    void setLayerVisible(size_t index, bool visible);
    void moveLayer(size_t fromIndex, size_t toIndex);
    void removeLayer(size_t index);

    size_t layerCount() const { return layers.size(); }

    std::vector<CompositorLayer> layers;

protected:
    RGBA computePixel(int worldX, int worldY) override;
    BoundsI computeBounds() override;

private:
    CompositorNode(){
        m_useCache = true;
    }
};
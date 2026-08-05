// name=node.cpp
#include "node.hpp"

#include <cstddef>
#include <iostream>

static inline uint64_t tileKey(int tx, int ty)
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(tx)) << 32) |
           static_cast<uint32_t>(ty);
}

RGBA Node::samplePixel(int worldX, int worldY)
{
    if(!m_useCache) return computePixel(worldX, worldY);

    int tileX = worldX >> 6;
    int tileY = worldY >> 6;
    int localX = worldX & (TILE_SIZE - 1);
    int localY = worldY & (TILE_SIZE - 1);

    const std::vector<RGBA>& tile = getTile(tileX, tileY);
    size_t idx = (size_t)localY * (size_t)TILE_SIZE + (size_t)localX;
    if(idx >= tile.size()) return transparent();
    return tile[idx];
}

void Node::refreshTile(int tileX, int tileY)
{
    uint64_t key = tileKey(tileX, tileY);

    // allocate tile
    std::vector<RGBA> tile;
    tile.resize((size_t)TILE_SIZE * (size_t)TILE_SIZE);

    int originX = (tileX << 6);
    int originY = (tileY << 6);

    for(int ly = 0; ly < TILE_SIZE; ++ly)
    {
        int worldY = originY + ly;
        for(int lx = 0; lx < TILE_SIZE; ++lx)
        {
            int worldX = originX + lx;
            size_t idx = (size_t)ly * (size_t)TILE_SIZE + (size_t)lx;
            tile[idx] = computePixel(worldX, worldY);
        }
    }

    m_tileCache.emplace(key, std::move(tile));
}

BoundsI Node::bounds()
{
    if(m_boundsGeneration == m_generation)
        return m_boundsCache;

    // recompute bounds and stamp
    m_boundsCache = computeBounds();
    m_boundsGeneration = m_generation;
    return m_boundsCache;
}

void Node::invalidate()
{
    ++m_generation;
    m_gpuBakeDirty = true;
    m_tileCache.clear();

    m_listeners.erase(
        std::remove_if(m_listeners.begin(), m_listeners.end(),
            [](const std::weak_ptr<Node>& w){ return w.expired(); }),
        m_listeners.end());

    for(auto& weak : m_listeners)
        if(auto listener = weak.lock())
            listener->invalidate();
}
void Node::invalidateTile(int tileX, int tileY)
{
    ++m_generation;          // cheap counter bump -> bounds() cache goes stale
    m_gpuBakeDirty = true;
    m_tileCache.erase(tileKey(tileX, tileY)); // per tile

    m_listeners.erase(
        std::remove_if(m_listeners.begin(), m_listeners.end(),
            [](const std::weak_ptr<Node>& w){ return w.expired(); }),
        m_listeners.end());

    for(auto& weak : m_listeners)
        if(auto listener = weak.lock())
            listener->invalidateTile(tileX, tileY);
}

const std::vector<RGBA>& Node::getTile(int tileX, int tileY)
{
    uint64_t key = tileKey(tileX, tileY);
    auto it = m_tileCache.find(key);
    if(it == m_tileCache.end())
    {
        refreshTile(tileX, tileY);
        it = m_tileCache.find(key);
    }
    return it->second;
}

// --- NODE INSPECTOR ---
#ifdef NODE_DEBUG_INSPECT

struct NodeInspector
{
    static std::string keyToString(uint64_t k)
    {
        uint32_t tx = static_cast<uint32_t>(k >> 32);
        uint32_t ty = static_cast<uint32_t>(k & 0xFFFFFFFFu);
        std::ostringstream ss;
        ss << "(" << static_cast<int32_t>(tx) << "," << static_cast<int32_t>(ty) << ")";
        return ss.str();
    }

    static void print(const Node& n)
    {
        std::cout << "NodeInspector: generation=" << n.m_generation
                  << " boundsGen=" << n.m_boundsGeneration
                  << " gpuBakeDirty=" << (n.m_gpuBakeDirty ? "yes" : "no")
                  << " useCache=" << (n.m_useCache ? "yes" : "no") << "\n";

        BoundsI b = n.m_boundsCache;
        std::cout << "  boundsCache: valid=" << b.valid;
        if(b.valid)
            std::cout << " [" << b.minX << "," << b.minY << " - " << b.maxX << "," << b.maxY << "]";
        std::cout << "\n";

        std::cout << "  tileCache: " << n.m_tileCache.size() << " tiles\n";
        size_t count = 0;
        for(auto &p : n.m_tileCache)
        {
            if(count++ >= 20) { std::cout << "   ...\n"; break; }
            std::cout << "   key=" << keyToString(p.first)
                      << " size=" << p.second.size() << "\n";
        }
    }

    static size_t tileCount(const Node& n) { return n.m_tileCache.size(); }
    static uint64_t generation(const Node& n) { return n.m_generation; }
    static bool gpuBakeDirty(const Node& n) { return n.m_gpuBakeDirty; }
};

#endif // NODE_DEBUG_INSPECT

RGBA RasterRootNode::computePixel(int worldX, int worldY)
{
    if(!m_raster) return transparent();

    int cx = RasterData::worldToChunk(worldX);
    int cy = RasterData::worldToChunk(worldY);

    Chunk* chunk = m_raster->tryGetChunk(cx, cy); // read-only, never creates
    if(!chunk) return transparent();

    return chunk->pixel(RasterData::worldToLocal(worldX), RasterData::worldToLocal(worldY));
}

BoundsI RasterRootNode::computeBounds()
{
    if(!m_raster)
    {
        BoundsI b; b.reset();
        return b;
    }
    // returnPixelBounds() will recompute RasterData's pixel bounds if dirty
    return m_raster->returnPixelBounds();
}

// --- COMPOSITOR ---
void CompositorNode::addLayer(std::shared_ptr<Node> input, float opacity, BlendMode blend, bool visible)
{
    listenTo(input, shared_from_this());
    layers.push_back({ std::move(input), opacity, blend, visible });
    invalidate();
}

namespace {
    inline uint8_t clamp255(float v) { return (uint8_t)std::clamp(v, 0.0f, 255.0f); }

    // standard "src over dst" alpha compositing (non-premultiplied inputs)
    RGBA blendNormal(RGBA dst, RGBA src, float opacity)
    {
        float srcA = (src.a / 255.0f) * opacity;
        if (srcA <= 0.0f) return dst;

        float dstA = dst.a / 255.0f;
        float outA = srcA + dstA * (1.0f - srcA);
        if (outA <= 0.0f) return RGBA{0,0,0,0};

        auto mix = [&](uint8_t s, uint8_t d) -> uint8_t {
            float sC = (s / 255.0f) * srcA;
            float dC = (d / 255.0f) * dstA * (1.0f - srcA);
            float outC = (sC + dC) / outA;
            return clamp255(outC * 255.0f);
        };
        return { mix(src.r, dst.r), mix(src.g, dst.g), mix(src.b, dst.b), clamp255(outA * 255.0f) };
    }

    RGBA blendMultiply(RGBA dst, RGBA src, float opacity)
    {
        RGBA mult = {
            (uint8_t)((int)src.r * (int)dst.r / 255),
            (uint8_t)((int)src.g * (int)dst.g / 255),
            (uint8_t)((int)src.b * (int)dst.b / 255),
            src.a
        };
        return blendNormal(dst, mult, opacity);
    }

    RGBA blendAdd(RGBA dst, RGBA src, float opacity)
    {
        RGBA add = {
            (uint8_t)std::min(255, (int)src.r + (int)dst.r),
            (uint8_t)std::min(255, (int)src.g + (int)dst.g),
            (uint8_t)std::min(255, (int)src.b + (int)dst.b),
            src.a
        };
        return blendNormal(dst, add, opacity);
    }

    RGBA blendScreen(RGBA dst, RGBA src, float opacity)
    {
        auto screen = [](uint8_t a, uint8_t b) -> uint8_t {
            return (uint8_t)(255 - (255 - a) * (255 - b) / 255);
        };
        RGBA scr = { screen(dst.r, src.r), screen(dst.g, src.g), screen(dst.b, src.b), src.a };
        return blendNormal(dst, scr, opacity);
    }
}

RGBA CompositorNode::computePixel(int worldX, int worldY)
{
    RGBA result = transparent();
    for (auto& layer : layers)
    {
        if (!layer.visible || !layer.input) continue;
        RGBA src = layer.input->samplePixel(worldX, worldY);

        switch (layer.blend)
        {
            case BlendMode::Normal:
                result = blendNormal(result, src, layer.opacity);
                break;
            case BlendMode::Multiply:
                result = blendMultiply(result, src, layer.opacity);
                break;
            case BlendMode::Add:
                result = blendAdd(result, src, layer.opacity);
                break;
            case BlendMode::Screen:
                result = blendScreen(result, src, layer.opacity);
                break;
        }
    }
    return result;
}

BoundsI CompositorNode::computeBounds()
{
    BoundsI result;
    result.reset();
    for (auto& layer : layers)
    {
        if (!layer.visible || !layer.input) continue;
        BoundsI b = layer.input->bounds();
        if (b.valid)
        {
            result.expand(b.minX, b.minY);
            result.expand(b.maxX, b.maxY);
        }
    }
    return result;
}

void CompositorNode::setLayerOpacity(size_t index, float opacity)
{
    if(index >= layers.size()) return;
    layers[index].opacity = opacity;
    invalidate();
}

void CompositorNode::setLayerBlend(size_t index, BlendMode blend)
{
    if(index >= layers.size()) return;
    layers[index].blend = blend;
    invalidate();
}

void CompositorNode::setLayerVisible(size_t index, bool visible)
{
    if(index >= layers.size()) return;
    layers[index].visible = visible;
    invalidate();
}

void CompositorNode::moveLayer(size_t fromIndex, size_t toIndex)
{
    if(fromIndex >= layers.size() || toIndex >= layers.size() || fromIndex == toIndex) return;
    CompositorLayer moved = std::move(layers[fromIndex]);
    layers.erase(layers.begin() + fromIndex);
    layers.insert(layers.begin() + toIndex, std::move(moved));
    invalidate();
}

void CompositorNode::removeLayer(size_t index)
{
    if(index >= layers.size()) return;
    layers.erase(layers.begin() + index);
    invalidate();
}
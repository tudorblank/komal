#pragma once
#include "node-base.hpp"

#include <unordered_set>

class CompositorNode;

class NodeCache{
public:
    SparseRasterGrid m_grid;

    Chunk& getOrCreateTile(CompositorNode& inputCompositor, int tileX, int tileY);
    RGBA lookupPixel(CompositorNode& inputCompositor, int worldX, int worldY);

    void invalidateAllTiles() { m_grid.clearAll(); }
    void invalidateTile(int tx, int ty) { m_grid.freeChunk(tx, ty); }
};

// ==== COMPOSITOR NODE ====
enum class CompositorType { Default, Master };

class CompositorNode : public Node{
private:
    struct Private { explicit Private() = default; };

public:
    // init
    explicit CompositorNode(Private) {}
    static std::shared_ptr<CompositorNode> create(CompositorType ctype)
    { 
        auto node = std::make_shared<CompositorNode>(Private{});
        node->m_meta.type = NodeType::Compositor;
        switch(ctype)
        {
            case CompositorType::Default:
                node->compType = CompositorType::Default;
                node->m_meta.label = "Compositor";
                break;
            case CompositorType::Master:
                node->compType = CompositorType::Master;
                node->m_meta.label = "Master Compositor";
                break;
        }
        return node;
    }
    CompositorType compType = CompositorType::Default;
    std::vector<std::shared_ptr<Node>> getInputs() const override { return m_layers; }

    // layer stack
    std::vector<std::shared_ptr<Node>> m_layers;

    void addLayer(std::shared_ptr<Node> input)
    {
        listenTo(input, shared_from_this());

        std::unordered_set<uint64_t> occupied;
        if(input) input->collectOccupiedTiles(occupied);

        m_layers.push_back(std::move(input));

        for(uint64_t key : occupied)
        {
            Key::XY p = Key::unpack(key);
            invalidateTile(p.x, p.y);
        }
    }
    void addLayerAt(std::shared_ptr<Node> input, size_t index)
    {
        listenTo(input, shared_from_this());
        std::unordered_set<uint64_t> occupied;
        if(input) input->collectOccupiedTiles(occupied);
        index = std::min(index, m_layers.size());
        m_layers.insert(m_layers.begin() + index, std::move(input));
        for(uint64_t key : occupied) { auto p = Key::unpack(key); invalidateTile(p.x, p.y); }
    }
    void removeLayer(size_t index)
    {
        if(index >= m_layers.size()) return;

        std::unordered_set<uint64_t> occupied;
        if(m_layers[index]) m_layers[index]->collectOccupiedTiles(occupied);

        m_layers.erase(m_layers.begin() + index);

        for(uint64_t key : occupied)
        {
            Key::XY p = Key::unpack(key);
            invalidateTile(p.x, p.y);
        }
    }
    std::unordered_set<uint64_t> moveLayer(size_t fromIndex, size_t toIndex)
    {
        if(fromIndex >= m_layers.size() || toIndex >= m_layers.size() || fromIndex == toIndex) return {};

        size_t lo = std::min(fromIndex, toIndex);
        size_t hi = std::max(fromIndex, toIndex);

        std::vector<std::unordered_set<uint64_t>> footprints(hi - lo + 1);
        for(size_t i = lo; i <= hi; i++)
            if(m_layers[i]) m_layers[i]->collectOccupiedTiles(footprints[i - lo]);

        std::unordered_set<uint64_t> affected;
        for(size_t a = 0; a < footprints.size(); a++)
            for(size_t b = a + 1; b < footprints.size(); b++)
                for(uint64_t key : footprints[a])
                    if(footprints[b].count(key))
                        affected.insert(key);

        auto moved = std::move(m_layers[fromIndex]);
        m_layers.erase(m_layers.begin() + fromIndex);
        m_layers.insert(m_layers.begin() + toIndex, std::move(moved));

        for(uint64_t key : affected) { Key::XY p = Key::unpack(key); invalidateTile(p.x, p.y); }
        return affected;
    }
    size_t layerCount() const { return m_layers.size(); }

    // compute
    RGBA computePixel(int worldX, int worldY) override
    {
        if(m_cache) return m_cache->lookupPixel(*this, worldX, worldY);
        return computeRaw(worldX, worldY);
    }
    RGBA computeRaw(int worldX, int worldY)
    {
        RGBA result = transparent();
        for(auto& layer : m_layers)
        {
            if(!layer) continue;
            result = over(result, layer->sampleBlended(worldX, worldY));
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
    void collectOccupiedTiles(std::unordered_set<uint64_t>& out) override
    {
        for(auto& layer : m_layers)
            if(layer) layer->collectOccupiedTiles(out);
    }

    // gpu bake
    bool m_gpuBakeDirty = true;
    bool needsGPUBake() const { return m_gpuBakeDirty; }
    void markGPUBaked()       { m_gpuBakeDirty = false; }

    // caching
    std::unique_ptr<NodeCache> m_cache;
    bool m_enableCache = false;

    void enableCache(bool enable)
    {
        if(enable == m_enableCache) return;

        m_enableCache = enable;
        m_gpuBakeDirty = true;

        if(enable) m_cache = std::make_unique<NodeCache>();
        else m_cache.reset();
    }
    const Chunk& getCachedTile(int tileX, int tileY)
    {
        if(!m_cache) enableCache(true);
        return m_cache->getOrCreateTile(*this, tileX, tileY);
    }
    void invalidateNode() override
    {
        m_gpuBakeDirty = true;
        if(m_cache) m_cache->invalidateAllTiles();
        Node::invalidateNode();
    }
    void invalidateTile(int tx, int ty) override
    {
        uint64_t key = Key::pack(tx, ty);
        if(m_pendingSyncKeys.insert(key).second)
            m_pendingSyncTiles.emplace_back(tx, ty);

        if(m_cache) m_cache->invalidateTile(tx, ty);
        Node::invalidateTile(tx, ty);
    }
    std::vector<std::pair<int,int>> m_pendingSyncTiles;
    std::unordered_set<uint64_t> m_pendingSyncKeys;
    std::vector<std::pair<int,int>> drainDirtySyncTiles()
    {
        auto result = std::move(m_pendingSyncTiles);
        m_pendingSyncTiles.clear();
        m_pendingSyncKeys.clear();
        return result;
    }

    void buildTile(Chunk& out, int tileX, int tileY);

private:
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
};
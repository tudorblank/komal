#include "canvas/tiles/atlas.hpp"

#include <algorithm>
#include <cstring>

TileAtlas::Page TileAtlas::createPage(const CanvasPipelines& p)
{
    Page page;
    WGPUTextureDescriptor td{};
    td.label         = sv("Atlas Page");
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = { (uint32_t)kPageSize, (uint32_t)kPageSize, 1 };
    td.format        = WGPUTextureFormat_RGBA8Unorm;
    td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    page.texture = wgpuDeviceCreateTexture(p.ctx.device, &td);
    page.view    = wgpuTextureCreateView(page.texture, nullptr);

    WGPUBindGroupEntry entries[2]{};
    entries[0].binding     = 0;
    entries[0].textureView = page.view;
    entries[1].binding     = 1;
    entries[1].sampler     = p.atlasSampler;
    WGPUBindGroupDescriptor gd{};
    gd.label      = sv("Atlas Page Bind Group");
    gd.layout     = p.atlasLayout;
    gd.entryCount = 2;
    gd.entries    = entries;
    page.bindGroup = wgpuDeviceCreateBindGroup(p.ctx.device, &gd);

    WGPUBufferDescriptor bd{};
    bd.label = sv("Atlas Page Instances");
    bd.size  = sizeof(ChunkInstance) * kSlotsTotal;
    bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    page.instanceBuffer = wgpuDeviceCreateBuffer(p.ctx.device, &bd);

    page.freeSlots.resize(kSlotsTotal);
    for (int i = 0; i < kSlotsTotal; ++i) page.freeSlots[i] = uint16_t(kSlotsTotal - 1 - i);
    return page;
}

void TileAtlas::upload(const CanvasPipelines& p, TileKey key, const RGBA* pixels)
{
    auto [it, inserted] = m_chunks.try_emplace(key);
    Chunk& c = it->second;
    if (inserted) {   // new tile: a slot, and the instances change
        auto page = std::find_if(m_pages.begin(), m_pages.end(), [](const Page& pg) { return !pg.freeSlots.empty(); });
        if (page == m_pages.end()) {
            m_pages.push_back(createPage(p));
            page = m_pages.end() - 1;
        }
        const auto [tx, ty] = TileUtils::unpackKey(key);
        c = { tx, ty, int(page - m_pages.begin()), page->freeSlots.back() };
        page->freeSlots.pop_back();
        m_layoutDirty = true;
    }
    // An existing tile only gets new pixels: its instance stays exactly the same.

    // Tile plus gutter (its edge pixels repeated), assembled once, uploaded in one write.
    constexpr int N = Tile::kSize, S = kSlotStride;
    static_assert(kGutter == 1, "staging below assumes a 1 px gutter");
    m_staging.resize(size_t(S) * S);
    for (int y = 0; y < S; ++y) {
        const RGBA* src = pixels + size_t(std::clamp(y - 1, 0, N - 1)) * N;
        RGBA* dst = m_staging.data() + size_t(y) * S;
        dst[0] = src[0];
        std::memcpy(dst + 1, src, N * sizeof(RGBA));
        dst[S - 1] = src[N - 1];
    }

    WGPUTexelCopyTextureInfo dst{};
    dst.texture = m_pages[c.page].texture;
    dst.origin  = { uint32_t((c.slot % kSlotsPerRow) * S), uint32_t((c.slot / kSlotsPerRow) * S), 0 };
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow  = S * sizeof(RGBA);
    layout.rowsPerImage = S;
    const WGPUExtent3D size = { uint32_t(S), uint32_t(S), 1 };
    wgpuQueueWriteTexture(p.ctx.queue, &dst, m_staging.data(), m_staging.size() * sizeof(RGBA), &layout, &size);
}

void TileAtlas::free(TileKey key)
{
    auto it = m_chunks.find(key);
    if (it == m_chunks.end()) return;
    m_pages[it->second.page].freeSlots.push_back(uint16_t(it->second.slot));
    m_chunks.erase(it);
    m_layoutDirty = true;
}

void TileAtlas::clear()
{
    for (Page& page : m_pages) {
        page.freeSlots.resize(kSlotsTotal);
        for (int i = 0; i < kSlotsTotal; ++i) page.freeSlots[i] = uint16_t(kSlotsTotal - 1 - i);
    }
    m_chunks.clear();
    m_layoutDirty = true;
}

void TileAtlas::release()
{
    for (Page& page : m_pages) {
        if (page.bindGroup)      wgpuBindGroupRelease(page.bindGroup);
        if (page.instanceBuffer) wgpuBufferRelease(page.instanceBuffer);
        if (page.view)           wgpuTextureViewRelease(page.view);
        if (page.texture)        wgpuTextureRelease(page.texture);
    }
    m_pages.clear();
    m_chunks.clear();
    m_layoutDirty = true;
}

ChunkInstance TileAtlas::makeInstance(const Chunk& c) const
{
    constexpr float N = (float)Tile::kSize;
    const float originX = float((c.slot % kSlotsPerRow) * kSlotStride + kGutter);
    const float originY = float((c.slot / kSlotsPerRow) * kSlotStride + kGutter);
    ChunkInstance inst{};
    inst.center[0]   = (float)TileUtils::tileToWorld(c.tx) + N * 0.5f;
    inst.center[1]   = (float)TileUtils::tileToWorld(c.ty) + N * 0.5f;
    inst.scale[0]    = N;
    inst.scale[1]    = N;
    inst.uvOffset[0] = originX / kPageSize;
    inst.uvOffset[1] = originY / kPageSize;
    inst.uvScale[0]  = N / kPageSize;
    inst.uvScale[1]  = N / kPageSize;
    inst.opacity     = m_opacity;
    return inst;
}

bool TileAtlas::updateInstances(const CanvasPipelines& p, int minTX, int minTY, int maxTX, int maxTY, float opacity)
{
    const bool rangeChanged = minTX != m_minTX || maxTX != m_maxTX || minTY != m_minTY || maxTY != m_maxTY;
    if (!m_layoutDirty && !rangeChanged && opacity == m_opacity) return false;
    m_minTX = minTX; m_maxTX = maxTX; m_minTY = minTY; m_maxTY = maxTY;
    m_opacity = opacity;
    m_layoutDirty = false;

    std::vector<std::vector<ChunkInstance>> next(m_pages.size());
    const auto add = [&](const Chunk& c) {
        if (c.tx >= minTX && c.tx <= maxTX && c.ty >= minTY && c.ty <= maxTY) next[c.page].push_back(makeInstance(c));
    };
    const double rangeTiles = double(maxTX - minTX + 1) * double(maxTY - minTY + 1);
    if (double(m_chunks.size()) <= rangeTiles) {   // walk whichever is smaller
        for (const auto& [key, c] : m_chunks) add(c);
    } else {
        for (int ty = minTY; ty <= maxTY; ++ty)
            for (int tx = minTX; tx <= maxTX; ++tx)
                if (auto it = m_chunks.find(TileUtils::packKey(tx, ty)); it != m_chunks.end()) add(it->second);
    }

    return uploadInstances(p, next);
}

void TileAtlas::beginInstances()
{
    m_building.assign(m_pages.size(), {});
}

bool TileAtlas::add(TileKey key, float x, float y, float size, float subX, float subY, float subSize)
{
    auto it = m_chunks.find(key);
    if (it == m_chunks.end()) return false;
    const Chunk& c = it->second;
    constexpr float N = (float)Tile::kSize;
    ChunkInstance inst{};
    inst.center[0]   = x + size * 0.5f;
    inst.center[1]   = y + size * 0.5f;
    inst.scale[0]    = size;
    inst.scale[1]    = size;
    inst.uvOffset[0] = (float((c.slot % kSlotsPerRow) * kSlotStride + kGutter) + subX * N) / kPageSize;
    inst.uvOffset[1] = (float((c.slot / kSlotsPerRow) * kSlotStride + kGutter) + subY * N) / kPageSize;
    inst.uvScale[0]  = subSize * N / kPageSize;
    inst.uvScale[1]  = subSize * N / kPageSize;
    m_building[c.page].push_back(inst);
    return true;
}

bool TileAtlas::endInstances(const CanvasPipelines& p, float opacity)
{
    for (auto& page : m_building)
        for (ChunkInstance& inst : page) inst.opacity = opacity;
    m_building.resize(m_pages.size());
    const bool changed = uploadInstances(p, m_building);
    m_building.clear();
    m_layoutDirty = true;   // updateInstances() must rebuild after a hand-made list
    return changed;
}

bool TileAtlas::uploadInstances(const CanvasPipelines& p, std::vector<std::vector<ChunkInstance>>& next)
{
    bool changed = false;
    for (size_t i = 0; i < m_pages.size(); ++i) {
        Page& page = m_pages[i];
        std::vector<ChunkInstance>& inst = next[i];
        const bool same = inst.size() == page.instances.size() &&
                          std::memcmp(inst.data(), page.instances.data(), inst.size() * sizeof(ChunkInstance)) == 0;
        if (same) continue;
        if (!inst.empty())
            wgpuQueueWriteBuffer(p.ctx.queue, page.instanceBuffer, 0, inst.data(), inst.size() * sizeof(ChunkInstance));
        page.instances = std::move(inst);
        changed = true;
    }
    return changed;
}

void TileAtlas::draw(WGPURenderPassEncoder pass) const
{
    for (const Page& page : m_pages) {
        if (page.instances.empty()) continue;
        wgpuRenderPassEncoderSetBindGroup(pass, 1, page.bindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, page.instanceBuffer, 0, page.instances.size() * sizeof(ChunkInstance));
        wgpuRenderPassEncoderDraw(pass, 4, (uint32_t)page.instances.size(), 0, 0);
    }
}

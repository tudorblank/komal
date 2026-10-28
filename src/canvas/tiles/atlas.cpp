#include "canvas/tiles/atlas.hpp"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------- pages

AtlasPages::Page AtlasPages::createPage(const CanvasPipelines& p)
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

    page.freeSlots.resize(kSlotsTotal);
    for (int i = 0; i < kSlotsTotal; ++i) page.freeSlots[i] = uint16_t(kSlotsTotal - 1 - i);
    return page;
}

AtlasPages::Slot AtlasPages::allocate(const CanvasPipelines& p)
{
    auto page = std::find_if(m_pages.begin(), m_pages.end(), [](const Page& pg) { return !pg.freeSlots.empty(); });
    if (page == m_pages.end()) {
        m_pages.push_back(createPage(p));
        page = m_pages.end() - 1;
    }
    const Slot s{ int(page - m_pages.begin()), page->freeSlots.back() };
    page->freeSlots.pop_back();
    return s;
}

void AtlasPages::upload(const CanvasPipelines& p, Slot s, const RGBA* pixels)
{
    // Tile plus gutter, uploaded in one write.
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
    dst.texture = m_pages[s.page].texture;
    dst.origin  = { uint32_t((s.slot % kSlotsPerRow) * S), uint32_t((s.slot / kSlotsPerRow) * S), 0 };
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow  = S * sizeof(RGBA);
    layout.rowsPerImage = S;
    const WGPUExtent3D size = { uint32_t(S), uint32_t(S), 1 };
    wgpuQueueWriteTexture(p.ctx.queue, &dst, m_staging.data(), m_staging.size() * sizeof(RGBA), &layout, &size);
}

void AtlasPages::free(Slot s)
{
    if (s.page >= 0 && s.page < int(m_pages.size())) m_pages[s.page].freed.push_back(uint16_t(s.slot));
}

void AtlasPages::freeNow(Slot s)
{
    if (s.page >= 0 && s.page < int(m_pages.size())) m_pages[s.page].freeSlots.push_back(uint16_t(s.slot));
}

void AtlasPages::recycle()
{
    for (Page& page : m_pages) {
        page.freeSlots.insert(page.freeSlots.end(), page.freed.begin(), page.freed.end());
        page.freed.clear();
    }
}

void AtlasPages::release()
{
    for (Page& page : m_pages) {
        if (page.bindGroup) wgpuBindGroupRelease(page.bindGroup);
        if (page.view)      wgpuTextureViewRelease(page.view);
        if (page.texture)   wgpuTextureRelease(page.texture);
    }
    m_pages.clear();
}

// ---------------------------------------------------------------- atlases

void TileAtlas::upload(const CanvasPipelines& p, TileKey key, const RGBA* pixels)
{
    auto [it, inserted] = m_chunks.try_emplace(key);
    if (inserted) it->second = m_pages->allocate(p);   // new tile: a slot
    m_pages->upload(p, it->second, pixels);
}

void TileAtlas::free(TileKey key)
{
    auto it = m_chunks.find(key);
    if (it == m_chunks.end()) return;
    m_pages->free(it->second);
    m_chunks.erase(it);
}

void TileAtlas::clear()
{
    for (const auto& [key, slot] : m_chunks) m_pages->freeNow(slot);
    m_chunks.clear();
}

void TileAtlas::release()
{
    clear();
    m_own.release();
}

// ---------------------------------------------------------------- draw lists

void TileDrawList::begin(const TileAtlas& atlas)
{
    m_atlas = &atlas;
    m_building.assign(atlas.m_pages->m_pages.size(), {});
}

bool TileDrawList::add(TileKey key, float x, float y, float size, float subX, float subY, float subSize)
{
    using P = AtlasPages;
    auto it = m_atlas->m_chunks.find(key);
    if (it == m_atlas->m_chunks.end()) return false;
    const P::Slot& c = it->second;
    constexpr float N = (float)Tile::kSize;
    ChunkInstance inst{};
    inst.center[0]   = x + size * 0.5f;
    inst.center[1]   = y + size * 0.5f;
    inst.scale[0]    = size;
    inst.scale[1]    = size;
    inst.uvOffset[0] = (float((c.slot % P::kSlotsPerRow) * P::kSlotStride + P::kGutter) + subX * N) / P::kPageSize;
    inst.uvOffset[1] = (float((c.slot / P::kSlotsPerRow) * P::kSlotStride + P::kGutter) + subY * N) / P::kPageSize;
    inst.uvScale[0]  = subSize * N / P::kPageSize;
    inst.uvScale[1]  = subSize * N / P::kPageSize;
    m_building[c.page].push_back(inst);
    return true;
}

bool TileDrawList::end(const CanvasPipelines& p, float opacity)
{
    const size_t pages = m_atlas ? m_atlas->m_pages->m_pages.size() : 0;
    m_building.resize(pages);
    if (m_pages.size() < pages) m_pages.resize(pages);
    bool changed = false;
    for (size_t i = 0; i < m_pages.size(); ++i) {
        Page& page = m_pages[i];
        std::vector<ChunkInstance> none;
        std::vector<ChunkInstance>& inst = i < pages ? m_building[i] : none;   // pages gone with a release: nothing
        for (ChunkInstance& c : inst) c.opacity = opacity;
        const bool same = inst.size() == page.instances.size() &&
                          std::memcmp(inst.data(), page.instances.data(), inst.size() * sizeof(ChunkInstance)) == 0;
        if (same) continue;
        if (!inst.empty()) {
            if (!page.buffer) {
                WGPUBufferDescriptor bd{};
                bd.label = sv("Atlas Page Instances");
                bd.size  = sizeof(ChunkInstance) * AtlasPages::kSlotsTotal;
                bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
                page.buffer = wgpuDeviceCreateBuffer(p.ctx.device, &bd);
            }
            wgpuQueueWriteBuffer(p.ctx.queue, page.buffer, 0, inst.data(), inst.size() * sizeof(ChunkInstance));
        }
        page.instances = std::move(inst);
        changed = true;
    }
    m_building.clear();
    return changed;
}

void TileDrawList::draw(WGPURenderPassEncoder pass) const
{
    if (!m_atlas) return;
    const std::vector<AtlasPages::Page>& atlasPages = m_atlas->m_pages->m_pages;
    const size_t pages = std::min(m_pages.size(), atlasPages.size());
    for (size_t i = 0; i < pages; ++i) {
        const Page& page = m_pages[i];
        if (page.instances.empty()) continue;
        wgpuRenderPassEncoderSetBindGroup(pass, 1, atlasPages[i].bindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, page.buffer, 0, page.instances.size() * sizeof(ChunkInstance));
        wgpuRenderPassEncoderDraw(pass, 4, (uint32_t)page.instances.size(), 0, 0);
    }
}

bool TileDrawList::empty() const
{
    return std::all_of(m_pages.begin(), m_pages.end(), [](const Page& p) { return p.instances.empty(); });
}

void TileDrawList::release()
{
    for (Page& page : m_pages)
        if (page.buffer) wgpuBufferRelease(page.buffer);
    m_pages.clear();
    m_building.clear();
    m_atlas = nullptr;
}

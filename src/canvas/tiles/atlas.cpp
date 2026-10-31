#include "canvas/tiles/atlas.hpp"
#include "gfx/error-scope.hpp"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------- pages

AtlasPages::Slot AtlasPages::allocate(const CanvasPipelines& p)
{
    const Slot s = m_slots.allocate(p.ctx.device);
    if (!s) {
        m_full = true;
        return s;
    }
    m_bindGroups.resize(m_slots.pageCount());
    if (!m_bindGroups[size_t(s.page)]) {   // a new page, or one made again: its bind group
        WGPUBindGroupEntry entries[2]{};
        entries[0].binding     = 0;
        entries[0].textureView = m_slots.texture(s.page).view();
        entries[1].binding     = 1;
        entries[1].sampler     = p.atlasSampler;
        WGPUBindGroupDescriptor gd{};
        gd.label      = sv("Atlas Page Bind Group");
        gd.layout     = p.atlasLayout;
        gd.entryCount = 2;
        gd.entries    = entries;
        m_bindGroups[size_t(s.page)].reset(wgpuDeviceCreateBindGroup(p.ctx.device, &gd));
    }
    return s;
}

void AtlasPages::recycle()
{
    m_slots.recycle();
    for (size_t i = 0; i < m_bindGroups.size(); ++i)
        if (!m_slots.texture(int(i))) m_bindGroups[i].reset();   // its texture went: the group with it
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
    dst.texture = m_slots.texture(s.page).texture();
    dst.origin  = { uint32_t(m_slots.originX(s)), uint32_t(m_slots.originY(s)), 0 };
    WGPUTexelCopyBufferLayout layout{};
    layout.bytesPerRow  = S * sizeof(RGBA);
    layout.rowsPerImage = S;
    const WGPUExtent3D size = { uint32_t(S), uint32_t(S), 1 };
    wgpuQueueWriteTexture(p.ctx.queue, &dst, m_staging.data(), m_staging.size() * sizeof(RGBA), &layout, &size);
}

void AtlasPages::release()
{
    m_bindGroups.clear();   // before the textures they bind
    m_slots.release();
    m_full = false;
}

// ---------------------------------------------------------------- atlases

bool TileAtlas::upload(const CanvasPipelines& p, TileKey key, const RGBA* pixels)
{
    auto [it, inserted] = m_chunks.try_emplace(key);
    if (inserted) {   // a new tile: a slot
        it->second = m_pages->allocate(p);
        if (!it->second) {
            m_chunks.erase(it);
            return false;
        }
    }
    m_pages->upload(p, it->second, pixels);
    return true;
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
    m_building.assign(atlas.m_pages->pageCount(), {});
}

bool TileDrawList::add(TileKey key, float x, float y, float size, float subX, float subY, float subSize)
{
    using P = AtlasPages;
    auto it = m_atlas->m_chunks.find(key);
    if (it == m_atlas->m_chunks.end()) return false;
    const P::Slot& c = it->second;
    constexpr float N = float(Tile::kSize);
    ChunkInstance inst{};
    inst.center[0]   = x + size * 0.5f;
    inst.center[1]   = y + size * 0.5f;
    inst.scale[0]    = size;
    inst.scale[1]    = size;
    const SlotPages& slots = m_atlas->m_pages->m_slots;
    inst.uvOffset[0] = (float(slots.originX(c) + P::kGutter) + subX * N) / P::kPageSize;
    inst.uvOffset[1] = (float(slots.originY(c) + P::kGutter) + subY * N) / P::kPageSize;
    inst.uvScale[0]  = subSize * N / P::kPageSize;
    inst.uvScale[1]  = subSize * N / P::kPageSize;
    m_building[size_t(c.page)].push_back(inst);
    return true;
}

bool TileDrawList::end(const CanvasPipelines& p, float opacity)
{
    const size_t pages = m_atlas ? m_atlas->m_pages->pageCount() : 0;
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
                GpuErrorScope scope(p.ctx.device);
                WGPUBufferDescriptor bd{};
                bd.label = sv("Atlas Page Instances");
                bd.size  = sizeof(ChunkInstance) * size_t(m_atlas->m_pages->m_slots.slotsPerPage());
                bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
                page.buffer.reset(wgpuDeviceCreateBuffer(p.ctx.device, &bd));
                if (scope.failed("tile instances")) page.buffer.reset();
            }
            if (page.buffer) wgpuQueueWriteBuffer(p.ctx.queue, page.buffer, 0, inst.data(), inst.size() * sizeof(ChunkInstance));
            else inst.clear();   // nothing to draw them from
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
    const std::vector<GpuObject<WGPUBindGroup>>& bindGroups = m_atlas->m_pages->m_bindGroups;
    const size_t pages = std::min(m_pages.size(), bindGroups.size());
    for (size_t i = 0; i < pages; ++i) {
        const Page& page = m_pages[i];
        if (page.instances.empty() || !bindGroups[i]) continue;   // a page that gave its texture back holds no tile
        wgpuRenderPassEncoderSetBindGroup(pass, 1, bindGroups[i], 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, page.buffer, 0, page.instances.size() * sizeof(ChunkInstance));
        wgpuRenderPassEncoderDraw(pass, 4, uint32_t(page.instances.size()), 0, 0);
    }
}

bool TileDrawList::empty() const
{
    return std::all_of(m_pages.begin(), m_pages.end(), [](const Page& p) { return p.instances.empty(); });
}

void TileDrawList::release()
{
    m_pages.clear();
    m_building.clear();
    m_atlas = nullptr;
}

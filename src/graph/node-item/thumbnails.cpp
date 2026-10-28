#include "graph/node-item/thumbnails.hpp"
#include "core/perf.hpp"
#include "core/project.hpp"
#include "gfx/math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr float kCheckerDark[3]  = { 52 / 255.0f, 52 / 255.0f, 52 / 255.0f };   // at the picture's corner
constexpr float kCheckerLight[3] = { 72 / 255.0f, 72 / 255.0f, 72 / 255.0f };
constexpr float kCheckerCell     = 8.0f;   // pixels

}

void NodeItemThumbnails::init(const CanvasPipelines* pipelines)
{
    m_pipelines = pipelines && pipelines->atlasPipeline ? pipelines : nullptr;
}

void NodeItemThumbnails::shutdown()
{
    for (auto& [id, t] : m_thumbs) release(*t);
    m_thumbs.clear();
    m_tilePages.release();
    for (Page& p : m_pages) {
        if (p.view)    wgpuTextureViewRelease(p.view);
        if (p.texture) wgpuTextureRelease(p.texture);
    }
    m_pages.clear();
    m_requested.clear();
    m_toRender.clear();
}

bool NodeItemThumbnails::get(NodeId id, View& out)
{
    m_requested.insert(id);
    auto it = m_thumbs.find(id);
    if (it == m_thumbs.end() || !it->second->shown) return false;
    const Thumb& t = *it->second;
    out.empty = t.empty;
    if (t.empty) return true;
    const float x = float((t.slot % kCols) * kWidth + t.fitX), y = float((t.slot / kCols) * kHeight + t.fitY);
    out.texture = (ImTextureID)(uintptr_t)m_pages[t.page].view;
    out.uv0     = ImVec2(x / kPageSize, y / kPageSize);
    out.uv1     = ImVec2((x + t.fitW) / kPageSize, (y + t.fitH) / kPageSize);
    out.fit0    = ImVec2(float(t.fitX) / kWidth, float(t.fitY) / kHeight);
    out.fit1    = ImVec2(float(t.fitX + t.fitW) / kWidth, float(t.fitY + t.fitH) / kHeight);
    return true;
}

void NodeItemThumbnails::forget(NodeId id)
{
    auto it = m_thumbs.find(id);
    if (it == m_thumbs.end()) return;
    release(*it->second);
    m_thumbs.erase(it);
}

void NodeItemThumbnails::release(Thumb& t)
{
    t.draw.release();
    t.camera.release();
    t.checker.release();
    t.atlas.clear();
    if (t.page >= 0) m_pages[t.page].freeSlots.push_back(t.slot);
    t.page = t.slot = -1;
}

bool NodeItemThumbnails::allocate(Thumb& t)
{
    auto page = std::find_if(m_pages.begin(), m_pages.end(), [](const Page& p) { return !p.freeSlots.empty(); });
    if (page == m_pages.end()) {
        Page p;
        WGPUTextureDescriptor td{};
        td.label         = sv("Node Thumbnails");
        td.dimension     = WGPUTextureDimension_2D;
        td.size          = { (uint32_t)kPageSize, (uint32_t)kPageSize, 1 };
        td.format        = m_pipelines->ctx.targetFormat;
        td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        p.texture = wgpuDeviceCreateTexture(m_pipelines->ctx.device, &td);
        if (!p.texture) return false;
        p.view = wgpuTextureCreateView(p.texture, nullptr);
        for (int i = kSlots; i-- > 0;) p.freeSlots.push_back(i);   // popped from the back: slot 0 first
        m_pages.push_back(std::move(p));
        page = m_pages.end() - 1;
    }
    t.page = int(page - m_pages.begin());
    t.slot = page->freeSlots.back();
    page->freeSlots.pop_back();
    return true;
}

// ---------------------------------------------------------------- CPU side

void NodeItemThumbnails::refresh(Project& project, double budgetMs)
{
    PerfScope perf(Perf::Thumbnails);
    m_nextFrameIn = std::numeric_limits<double>::infinity();   // Application::kNoFrame (its header pulls in the UI)
    m_toRender.clear();
    if (!m_pipelines) {
        m_requested.clear();
        return;
    }
    m_tilePages.recycle();   // no thumbnail drawn last frame reads them any more
    const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(budgetMs));

    for (NodeId id : m_requested) {
        Node* n = project.find(id);
        if (!n) continue;
        std::unique_ptr<Thumb>& tp = m_thumbs[id];
        if (!tp) {
            tp = std::make_unique<Thumb>();
            tp->atlas.share(m_tilePages);
            tp->watch = n->watchOutput();
        }
        Thumb& t = *tp;
        update(*n, t, deadline);
        if (!t.pending.empty()) m_nextFrameIn = 0.0;   // the rest stay requested by being drawn again
        if (t.empty) { t.shown = true; continue; }
        if (t.stale && !(t.rebuilding && !t.pending.empty()) && prepareRender(t)) m_toRender.push_back(id);
    }
    m_requested.clear();
}

void NodeItemThumbnails::update(Node& n, Thumb& t, Clock::time_point deadline)
{
    TileSet changed;
    bool restart = t.watch->take(changed);
    // Fitted again when the output changed (not while a drag previews an offset: that moves the
    // bounds alone, and the pixels stay until it's applied).
    if (n.revision() != t.revision) {
        t.revision = n.revision();
        const BoundsI b = n.bounds();
        if (b != t.bounds) { t.bounds = b; t.stale = true; }
        t.empty = b.empty();
        // The level whose texel covers at most a thumbnail pixel.
        int level = 0;
        if (!t.empty) {
            const float fit = std::min(float(kWidth) / float(b.width()), float(kHeight) / float(b.height()));
            if (fit < 1.0f) level = std::clamp(int(std::floor(std::log2(1.0f / fit))), 0, kMaxLevel);
        }
        if (level != t.level) { t.level = level; restart = true; }
    }
    if (restart) {
        t.tiles.clear();
        t.atlas.clear();
        t.pending = n.footprint();
        t.rebuilding = true;
        t.stale = true;
    } else {
        t.pending.insert(changed.begin(), changed.end());
    }
    if (t.pending.empty()) return;

    std::vector<TileKey> done;
    TileSet touched;   // tiles of the level
    for (TileKey key : t.pending) {
        if (Clock::now() >= deadline) break;
        fold(n, t, key, touched);
        done.push_back(key);
    }
    for (TileKey key : done) t.pending.erase(key);
    for (TileKey key : touched) {
        const auto [tx, ty] = TileUtils::unpackKey(key);
        const Tile* tile = t.tiles.getReadTile(tx, ty);
        if (tile && !tile->isEmpty()) {
            t.atlas.upload(*m_pipelines, key, tile->getReadAll());
        } else {
            t.tiles.freeTile(tx, ty);
            t.atlas.free(key);
        }
    }
    if (!touched.empty()) t.stale = true;
    if (t.pending.empty()) t.rebuilding = false;
}

void NodeItemThumbnails::fold(Node& n, Thumb& t, TileKey key, TileSet& touched)
{
    const auto [sx, sy] = TileUtils::unpackKey(key);
    const int L = t.level, block = 1 << L, sub = Tile::kSize >> L;   // a texel averages block x block pixels
    const int tx = sx >> L, ty = sy >> L;                             // its tile at the level
    const int ox = (sx - (tx << L)) * sub, oy = (sy - (ty << L)) * sub;   // its part of that tile
    const Tile* src = renderTile(n, sx, sy, m_render);
    Tile* dst = t.tiles.getWriteTile(tx, ty);
    if (!src && !dst) return;   // nothing before, nothing now
    if (!dst) {
        dst = &t.tiles.writeOrCreateTile(tx, ty);
        dst->clear();
    }
    {
        Tile::BulkWrite w(*dst, Tile::BulkWrite::Trusted);   // averages of premultiplied pixels stay premultiplied
        RGBA* d = w.data();
        const RGBA* s = src ? src->getReadAll() : nullptr;
        const int shift = 2 * L;
        const uint32_t half = (1u << shift) >> 1;
        for (int y = 0; y < sub; ++y)
            for (int x = 0; x < sub; ++x) {
                RGBA& out = d[size_t(oy + y) * Tile::kSize + ox + x];
                if (!s) { out = transparent(); continue; }
                uint32_t r = 0, g = 0, b = 0, a = 0;
                for (int j = 0; j < block; ++j) {
                    const RGBA* row = s + size_t(y * block + j) * Tile::kSize + x * block;
                    for (int i = 0; i < block; ++i) { r += row[i].r; g += row[i].g; b += row[i].b; a += row[i].a; }
                }
                out = { uint8_t((r + half) >> shift), uint8_t((g + half) >> shift), uint8_t((b + half) >> shift),
                        uint8_t((a + half) >> shift) };
            }
        w.markChanged();
    }
    touched.insert(TileUtils::packKey(tx, ty));
}

bool NodeItemThumbnails::prepareRender(Thumb& t)
{
    if (t.page < 0 && !allocate(t)) return false;
    const GpuHandles& ctx = m_pipelines->ctx;
    if (!t.camera)  t.camera.create(ctx.device, m_pipelines->cameraLayout, sizeof(Mat4::m), "Thumbnail Camera");
    if (!t.checker) t.checker.create(ctx.device, m_pipelines->checkerLayout, sizeof(CheckerParams), "Thumbnail Checker");
    if (!t.camera || !t.checker) return false;

    // Fitted, proportions kept, centred in the slot.
    const BoundsI& b = t.bounds;
    const float bw = float(b.width()), bh = float(b.height());
    const float fit = std::min(float(kWidth) / bw, float(kHeight) / bh);
    t.fitW = std::clamp(int(std::lround(bw * fit)), 1, kWidth);
    t.fitH = std::clamp(int(std::lround(bh * fit)), 1, kHeight);
    t.fitX = (kWidth - t.fitW) / 2;
    t.fitY = (kHeight - t.fitH) / 2;

    // World -> the picture's viewport.
    const float sx = float(t.fitW) / bw, sy = float(t.fitH) / bh;
    const Mat4 viewProj = Mat4::ortho(float(t.fitW), float(t.fitH)) * Mat4::translateScale(-float(b.minX) * sx, -float(b.minY) * sy, sx, sy);
    t.camera.write(ctx.queue, viewProj.m, sizeof(viewProj.m));

    // In page pixels: the pattern starts at the picture's corner.
    CheckerParams c{};
    for (int i = 0; i < 3; ++i) { c.colorA[i] = kCheckerDark[i]; c.colorB[i] = kCheckerLight[i]; }
    c.colorA[3] = c.colorB[3] = 1.0f;
    c.origin[0] = float((t.slot % kCols) * kWidth + t.fitX);
    c.origin[1] = float((t.slot / kCols) * kHeight + t.fitY);
    c.cell      = kCheckerCell;
    t.checker.write(ctx.queue, &c, sizeof c);

    const float size = float(Tile::kSize << t.level);
    t.draw.begin(t.atlas);
    t.tiles.forEachRead([&](int tx, int ty, const Tile&) { t.draw.add(TileUtils::packKey(tx, ty), float(tx) * size, float(ty) * size, size); });
    t.draw.end(*m_pipelines, 1.0f);
    return true;
}

// ---------------------------------------------------------------- GPU side

void NodeItemThumbnails::renderGpu(WGPUCommandEncoder enc)
{
    if (m_toRender.empty()) return;
    // One pass per page: each picture in its own viewport, the rest of the page kept.
    std::vector<Thumb*> thumbs;
    for (NodeId id : m_toRender)
        if (auto it = m_thumbs.find(id); it != m_thumbs.end() && it->second->page >= 0) thumbs.push_back(it->second.get());
    std::sort(thumbs.begin(), thumbs.end(), [](const Thumb* a, const Thumb* b) { return a->page < b->page; });

    for (size_t i = 0; i < thumbs.size();) {
        const int page = thumbs[i]->page;
        WGPURenderPassColorAttachment ca{};
        ca.view       = m_pages[page].view;
        ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp     = WGPULoadOp_Load;
        ca.storeOp    = WGPUStoreOp_Store;
        WGPURenderPassDescriptor rp{};
        rp.label                = sv("Thumbnails Pass");
        rp.colorAttachmentCount = 1;
        rp.colorAttachments     = &ca;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        for (; i < thumbs.size() && thumbs[i]->page == page; ++i) {
            Thumb& t = *thumbs[i];
            const uint32_t x = uint32_t((t.slot % kCols) * kWidth + t.fitX), y = uint32_t((t.slot / kCols) * kHeight + t.fitY);
            wgpuRenderPassEncoderSetViewport(pass, float(x), float(y), float(t.fitW), float(t.fitH), 0.0f, 1.0f);
            wgpuRenderPassEncoderSetScissorRect(pass, x, y, uint32_t(t.fitW), uint32_t(t.fitH));
            wgpuRenderPassEncoderSetPipeline(pass, m_pipelines->checkerPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, t.checker.group, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            if (!t.draw.empty()) {
                wgpuRenderPassEncoderSetPipeline(pass, m_pipelines->atlasPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, t.camera.group, 0, nullptr);
                t.draw.draw(pass);
            }
            t.stale = false;
            t.shown = true;
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
    m_toRender.clear();
}

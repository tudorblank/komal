#include "canvas/node-thumbnails.hpp"
#include "gfx/render-pass.hpp"
#include "base/perf.hpp"
#include "base/frames.hpp"
#include "base/palette.hpp"
#include "canvas/preview/preview.hpp"
#include "document/project.hpp"
#include "base/math.hpp"

#include <algorithm>
#include <cmath>

void NodeThumbnails::init(const CanvasPipelines* pipelines, TileCache& tiles, const Preview& preview)
{
    m_pipelines = pipelines && pipelines->atlasPipeline ? pipelines : nullptr;
    m_tiles     = &tiles;
    m_preview   = &preview;
    if (m_pipelines)   // the pictures are rendered (or a preview's copied) into their slots, then sampled by ImGui
        m_pages.configure({ .slotWidth = kWidth, .slotHeight = kHeight, .pageSize = kPageSize, .format = m_pipelines->ctx.targetFormat,
                            .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst,
                            .label = "Node Thumbnails" });
}

void NodeThumbnails::shutdown()
{
    for (auto& [id, t] : m_thumbs) release(*t);
    m_thumbs.clear();
    m_pictures.clear();
    m_toPreview.clear();
    m_pages.release();
    m_requested.clear();
    m_toRender.clear();
}

bool NodeThumbnails::get(NodeId id, View& out)
{
    m_requested.insert(id);
    auto it = m_thumbs.find(id);
    if (it == m_thumbs.end() || !it->second->shown) return false;
    const Thumb& t = *it->second;
    out.empty = t.empty;
    if (t.empty) return true;
    const float x = float(m_pages.originX(t.slot) + t.fitX), y = float(m_pages.originY(t.slot) + t.fitY);
    out.texture = (ImTextureID)(uintptr_t)m_pages.texture(t.slot.page).view();
    out.uv0     = ImVec2(x / kPageSize, y / kPageSize);
    out.uv1     = ImVec2((x + t.fitW) / kPageSize, (y + t.fitH) / kPageSize);
    out.fit0    = ImVec2(float(t.fitX) / kWidth, float(t.fitY) / kHeight);
    out.fit1    = ImVec2(float(t.fitX + t.fitW) / kWidth, float(t.fitY + t.fitH) / kHeight);
    return true;
}

void NodeThumbnails::forget(NodeId id)
{
    auto it = m_thumbs.find(id);
    if (it == m_thumbs.end()) return;
    release(*it->second);
    m_thumbs.erase(it);
}

void NodeThumbnails::release(Thumb& t)
{
    t.draw.release();
    t.camera.release();
    t.checker.release();
    t.tiles.reset();
    if (t.slot) m_pages.freeNow(t.slot);   // nothing draws a forgotten node's picture
    t.slot = {};
}

// ---------------------------------------------------------------- CPU side

void NodeThumbnails::refresh(const Project& project, const Deadline& budget)
{
    PerfScope perf(Perf::Thumbnails);
    m_nextFrameIn = kNoFrame;
    m_toRender.clear();
    m_toPreview.clear();
    if (!m_pipelines) {
        m_requested.clear();
        return;
    }
    m_budget = budget;
    const bool previewing = m_preview->active();
    absl::erase_if(m_pictures, [&](const auto& kv) { return !previewing || !m_preview->plan().reaches(kv.first); });

    for (NodeId id : m_requested) {
        const NodePtr n = project.findPtr(id);
        if (!n) continue;
        std::unique_ptr<Thumb>& tp = m_thumbs[id];
        if (!tp) {
            tp = std::make_unique<Thumb>();
            tp->tiles = m_tiles->output(n);
        }
        Thumb& t = *tp;
        if (previewing && m_preview->plan().reaches(id)) {   // its slot shows the preview; its tiles wait
            m_toPreview.push_back(id);
            continue;
        }
        const bool complete = update(*n, t, budget);
        if (!complete) m_nextFrameIn = 0.0;   // the rest stay requested by being drawn again
        if (t.empty) { t.shown = true; continue; }
        if (complete && (t.stale || t.tiles->revision(t.level) != t.seen) && prepareRender(t)) m_toRender.push_back(id);
    }
    m_requested.clear();
}

bool NodeThumbnails::update(const Node& n, Thumb& t, const Deadline& budget)
{
    const BoundsI b = n.bounds();
    if (b != t.bounds) { t.bounds = b; t.stale = true; }
    t.empty = b.empty();
    if (t.empty) return true;
    const int level = lodLevel(fitScale(b, float(kWidth), float(kHeight)));   // a texel at most a thumbnail pixel
    if (level != t.level) { t.level = level; t.stale = true; }
    for (TileKey key : t.tiles->footprint(level))
        if (!t.tiles->build(budget, *m_pipelines, level, key)) return false;
    return true;
}

bool NodeThumbnails::prepareRender(Thumb& t)
{
    if (!t.slot) t.slot = m_pages.allocate(m_pipelines->ctx.device);
    if (!t.slot) return false;
    const GpuHandles& ctx = m_pipelines->ctx;
    if (!t.camera)  t.camera.create(ctx.device, m_pipelines->cameraLayout, sizeof(Mat4::m), "Thumbnail Camera");
    if (!t.checker) t.checker.create(ctx.device, m_pipelines->checkerLayout, sizeof(CheckerParams), "Thumbnail Checker");
    if (!t.camera || !t.checker) return false;

    // Fitted, proportions kept, centred in the slot.
    const BoundsI& b = t.bounds;
    const PictureFit fit = fitInto(b, kWidth, kHeight);
    t.fitW = int(fit.width);
    t.fitH = int(fit.height);
    t.fitX = (kWidth - t.fitW) / 2;
    t.fitY = (kHeight - t.fitH) / 2;

    // World -> the picture's viewport.
    const float sx = float(t.fitW) / float(b.width()), sy = float(t.fitH) / float(b.height());
    const Mat4 viewProj = Mat4::ortho(float(t.fitW), float(t.fitH)) * Mat4::translateScale(-float(b.minX) * sx, -float(b.minY) * sy, sx, sy);
    t.camera.write(ctx.queue, viewProj.m, sizeof(viewProj.m));

    // In page pixels: the pattern starts at the picture's corner.
    const CheckerParams c = pictureChecker(float(m_pages.originX(t.slot) + t.fitX), float(m_pages.originY(t.slot) + t.fitY));
    t.checker.write(ctx.queue, &c, sizeof c);

    const float size = float(Tile::kSize << t.level);
    t.draw.begin(t.tiles->atlas(t.level));
    for (TileKey key : t.tiles->footprint(t.level)) {
        const auto [tx, ty] = TileUtils::unpackKey(key);
        t.draw.add(key, float(tx) * size, float(ty) * size, size);   // empty tiles aren't in the atlas: skipped
    }
    t.draw.end(*m_pipelines, 1.0f);
    t.seen = t.tiles->revision(t.level);
    return true;
}

// ---------------------------------------------------------------- GPU side

void NodeThumbnails::renderGpu(WGPUCommandEncoder enc)
{
    for (NodeId id : m_toPreview)
        if (auto it = m_thumbs.find(id); it != m_thumbs.end()) renderPreview(enc, id, *it->second);
    m_toPreview.clear();
    if (m_toRender.empty()) return;
    // One pass per page: each picture in its own viewport, the rest of the page kept.
    std::vector<Thumb*> thumbs;
    for (NodeId id : m_toRender)
        if (auto it = m_thumbs.find(id); it != m_thumbs.end() && it->second->slot) thumbs.push_back(it->second.get());
    std::sort(thumbs.begin(), thumbs.end(), [](const Thumb* a, const Thumb* b) { return a->slot.page < b->slot.page; });

    for (size_t i = 0; i < thumbs.size();) {
        const int page = thumbs[i]->slot.page;
        const GpuRenderPass pass(enc, m_pages.texture(page).view(), GpuRenderPass::Load::Keep, "Thumbnails Pass");
        for (; i < thumbs.size() && thumbs[i]->slot.page == page; ++i) {
            Thumb& t = *thumbs[i];
            pass.confine(uint32_t(m_pages.originX(t.slot) + t.fitX), uint32_t(m_pages.originY(t.slot) + t.fitY), uint32_t(t.fitW), uint32_t(t.fitH));
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
    }
    m_toRender.clear();
}

void NodeThumbnails::renderPreview(WGPUCommandEncoder enc, NodeId id, Thumb& t)
{
    std::unique_ptr<PreviewPicture>& picture = m_pictures[id];
    if (!picture) picture = std::make_unique<PreviewPicture>();
    switch (picture->render(*m_preview, id, kWidth, kHeight, *m_pipelines, enc, m_budget)) {
    case PreviewPicture::Result::Rendered:
        break;
    case PreviewPicture::Result::Empty:
        t.empty = t.shown = t.stale = true;
        return;
    case PreviewPicture::Result::Waiting:
        m_nextFrameIn = 0.0;
        return;
    case PreviewPicture::Result::NotReached:
    case PreviewPicture::Result::Unchanged:
        return;
    }
    if (!t.slot) t.slot = m_pages.allocate(m_pipelines->ctx.device);
    if (!t.slot) return;
    t.fitW = int(picture->width());
    t.fitH = int(picture->height());
    t.fitX = (kWidth - t.fitW) / 2;
    t.fitY = (kHeight - t.fitH) / 2;
    WGPUTexelCopyTextureInfo src{};
    src.texture = picture->texture().texture();
    WGPUTexelCopyTextureInfo dst{};
    dst.texture = m_pages.texture(t.slot.page).texture();
    dst.origin  = { uint32_t(m_pages.originX(t.slot) + t.fitX), uint32_t(m_pages.originY(t.slot) + t.fitY), 0 };
    const WGPUExtent3D size = { uint32_t(t.fitW), uint32_t(t.fitH), 1 };
    wgpuCommandEncoderCopyTextureToTexture(enc, &src, &dst, &size);
    t.empty = false;
    t.shown = true;
    t.stale = true;   // once the edit is over, its own picture again
}

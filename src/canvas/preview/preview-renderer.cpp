#include "canvas/preview/preview-renderer.hpp"
#include "canvas/preview/preview.hpp"
#include "document/project.hpp"
#include "gfx/render-pass.hpp"
#include "node/node-kinds.hpp"

#include <algorithm>
#include <cmath>

namespace {

int maskSlot(NodeSignal s, NodeChannel c) { return int(s) * kChannelCount + int(c); }

}

// ---------------------------------------------------------------- spaces

PreviewSpace PreviewSpace::ofView(const CanvasFrame& frame)
{
    PreviewSpace s;
    s.camera  = frame.camera;
    s.viewW   = frame.target.viewW;
    s.viewH   = frame.target.viewH;
    s.pixelW  = frame.target.pixelW;
    s.pixelH  = frame.target.pixelH;
    s.visible = frame.view;
    s.level   = frame.lodLevel();
    return s;
}

PreviewSpace PreviewSpace::fitted(const BoundsI& b, uint32_t width, uint32_t height)
{
    PreviewSpace s;
    const float zoom = fitScale(b, float(width), float(height));
    s.camera.zoom = zoom;
    s.camera.pan  = { -float(b.minX) * zoom, -float(b.minY) * zoom };   // the bounds' corner at the picture's
    s.viewW   = float(width);
    s.viewH   = float(height);
    s.pixelW  = width;
    s.pixelH  = height;
    s.visible = { float(b.minX), float(b.minY), float(b.maxX + 1), float(b.maxY + 1) };
    s.level   = lodLevel(zoom);
    return s;
}

// ---------------------------------------------------------------- passes, from the plan

bool PreviewRenderer::plan(const Preview& preview, NodeId node)
{
    if (preview.planSerial() != m_serial) {
        forget();
        m_serial = preview.planSerial();
    }
    m_preview = &preview;
    m_ops.clear();
    m_built.clear();
    m_target = preview.active() ? preview.plan().indexOf(node) : -1;
    if (m_target < 0) return false;
    build(m_target, {});   // its output: the last op
    return true;
}

BoundsI PreviewRenderer::bounds() const
{
    return m_target >= 0 ? m_preview->bounds(m_preview->plan().nodes()[size_t(m_target)].node->id()) : BoundsI{};
}

int PreviewRenderer::push(Op op)
{
    m_ops.push_back(std::move(op));
    return int(m_ops.size()) - 1;
}

int PreviewRenderer::leafOp(int leaf, Vec2 offset)
{
    if (leaf < 0) return -1;
    for (const Built& b : m_built)
        if (b.leaf == leaf && b.offset == offset) return b.op;
    Op op;
    op.kind   = Op::Kind::Leaf;
    op.leaf   = leaf;
    op.offset = offset;
    const int i = push(std::move(op));
    m_built.push_back({ -1, leaf, offset, i });
    return i;
}

int PreviewRenderer::opFor(PreviewPlan::Source src, Vec2 offset)
{
    return src.node >= 0 ? build(src.node, offset) : leafOp(src.leaf, offset);
}

int PreviewRenderer::build(int node, Vec2 offset)
{
    for (const Built& b : m_built)   // reached twice: built once per offset, shared
        if (b.node == node && b.offset == offset) return b.op;

    const PreviewPlan::Node& s = m_preview->plan().nodes()[size_t(node)];
    const Node& n = *s.node;

    int wet = -1, dry = -1;
    if (m_preview->wetOn(n)) {
        if (const std::optional<Vec2> by = m_preview->moveOffset(n)) {
            wet = opFor(s.input, { offset.x + by->x, offset.y + by->y });   // a move moves its input
        } else if (inputShape(n) == InputShape::One) {
            wet = opFor(s.input, offset);
        } else if (!s.layers.empty()) {   // a stack the edit reaches: recomposed
            Op stack;
            stack.kind = Op::Kind::Stack;
            for (PreviewPlan::Source l : s.layers) {
                Layer layer;
                if (l.node >= 0) {
                    layer.op = build(l.node, offset);
                } else {
                    layer.leaf   = l.leaf;
                    layer.offset = offset;
                }
                stack.layers.push_back(layer);
            }
            wet = push(std::move(stack));
        } else if (s.stroke >= 0) {   // the raster being painted: the stroke over its pixels
            Op stroke;
            stroke.kind     = Op::Kind::Stroke;
            stroke.pixels   = leafOp(s.wet, offset);
            stroke.coverage = leafOp(s.stroke, offset);
            wet = push(std::move(stroke));
        } else {
            wet = leafOp(s.wet, offset);
        }
    }
    if (m_preview->dryOn(n)) dry = opFor(s.input, offset);

    Op signal;
    signal.kind = Op::Kind::Signal;
    signal.node = node;
    signal.wet  = wet;
    signal.dry  = dry;
    for (int i = 0; i < kPreviewMaskTextures; ++i) signal.masks[i] = -1;
    for (int sig = 0; sig < kSignalCount; ++sig)
        for (int ch = 0; ch < kChannelCount; ++ch)
            if (s.masks[sig][ch]) signal.masks[maskSlot(NodeSignal(sig), NodeChannel(ch))] = opFor(s.masks[sig][ch], offset);
    const int op = push(std::move(signal));
    m_built.push_back({ node, -1, offset, op });
    return op;
}

// ---------------------------------------------------------------- leaves

bool PreviewRenderer::catchUpLeaf(int leaf, Vec2 offset, const PreviewSpace& space, const CanvasPipelines& p, const Deadline& budget)
{
    // The leaf's tiles that land in the picture once shifted by the offset.
    using TileUtils::worldToTileClamped;
    const int L = space.level;
    const TileRect range{ worldToTileClamped(space.visible.minX - offset.x) >> L, worldToTileClamped(space.visible.minY - offset.y) >> L,
                          worldToTileClamped(space.visible.maxX - offset.x) >> L, worldToTileClamped(space.visible.maxY - offset.y) >> L };
    RasterTiles& tiles = m_preview->leaf(leaf);
    bool ready = true;
    forEachTileIn(tiles.footprint(L), range, [&](TileKey key) {
        if (tiles.build(budget, p, L, key)) return true;
        ready = false;
        return false;
    });
    return ready;
}

bool PreviewRenderer::catchUp(const PreviewSpace& space, const CanvasPipelines& p, const Deadline& budget)
{
    for (const Op& op : m_ops) {
        if (op.kind == Op::Kind::Leaf && !catchUpLeaf(op.leaf, op.offset, space, p, budget)) return false;
        for (const Layer& l : op.layers)
            if (l.leaf >= 0 && !catchUpLeaf(l.leaf, l.offset, space, p, budget)) return false;
    }
    return true;
}

PreviewRenderer::FrameKey PreviewRenderer::frameKey(const PreviewSpace& space) const
{
    FrameKey k;
    k.serial   = m_serial;
    k.target   = m_target;
    k.edit     = m_preview->edit();
    k.panX     = space.camera.pan.x;
    k.panY     = space.camera.pan.y;
    k.zoom     = space.camera.zoom;
    k.rotation = space.camera.rotation;
    k.viewW    = space.viewW;
    k.viewH    = space.viewH;
    k.width    = space.pixelW;
    k.height   = space.pixelH;
    k.level    = space.level;
    for (const Op& op : m_ops) {
        if (op.kind == Op::Kind::Leaf) k.revisions += m_preview->leaf(op.leaf).revision(space.level);
        for (const Layer& l : op.layers)
            if (l.leaf >= 0) k.revisions += m_preview->leaf(l.leaf).revision(space.level);
    }
    return k;
}

bool PreviewRenderer::render(const Preview& preview, const PreviewSpace& space, const CanvasPipelines& p, WGPUCommandEncoder enc,
                             const Deadline& budget)
{
    m_preview = &preview;
    if (m_target < 0 || m_ops.empty() || space.pixelW == 0 || space.pixelH == 0) return false;
    m_ready = catchUp(space, p, budget);
    if (!m_ready) return false;   // the last result stays until every input is there
    const FrameKey key = frameKey(space);
    if (m_hasResult && key == m_rendered) return false;   // the result already shows exactly this
    if (!execute(space, p, enc)) return false;
    m_rendered  = key;
    m_hasResult = true;
    m_result    = m_ops.size() - 1;
    return true;
}

// ---------------------------------------------------------------- GPU

GpuTexture* PreviewRenderer::target(const PreviewSpace& space, const CanvasPipelines& p, size_t i)
{
    if (space.pixelW > m_targetW || space.pixelH > m_targetH) {   // they only grow: a smaller picture uses their corner
        m_targets.clear();
        m_content.clear();
        m_targetW = std::max(m_targetW, space.pixelW);
        m_targetH = std::max(m_targetH, space.pixelH);
    }
    m_content.resize(std::max(m_content.size(), i + 1));
    while (m_targets.size() <= i)
        if (!m_targets.emplace_back().create(p.ctx.device, { m_targetW, m_targetH, p.ctx.targetFormat,
                                             WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc,
                                             "Preview Target" })) {
            m_targets.pop_back();
            return nullptr;
        }
    return &m_targets[i];
}

GpuUniform* PreviewRenderer::uniform(const CanvasPipelines& p, UniformPool& pool, WGPUBindGroupLayout layout, uint64_t size)
{
    if (pool.used == pool.items.size() && !pool.items.emplace_back().create(p.ctx.device, layout, size, "Preview Uniform")) {
        pool.items.pop_back();
        return nullptr;
    }
    return &pool.items[pool.used++];
}

void PreviewRenderer::drawLeaf(WGPURenderPassEncoder pass, int leaf, Vec2 offset, const PreviewSpace& space, const CanvasPipelines& p)
{
    const int L = space.level;
    RasterTiles& tiles = m_preview->leaf(leaf);
    // Every tile of the leaf at this level (the GPU clips what's outside): one list for every
    // offset this frame, since buffer writes land before the passes run.
    std::unique_ptr<LeafDraw>& slot = m_draws[leaf * RasterTiles::kLevels + L];
    if (!slot) slot = std::make_unique<LeafDraw>();
    LeafDraw& d = *slot;
    if (d.placed != tiles.revision(L)) {
        const float size = float(Tile::kSize << L);
        d.draw.begin(tiles.atlas(L));
        for (TileKey key : tiles.footprint(L)) {
            const auto t = TileUtils::unpackKey(key);
            d.draw.add(key, float(t.x) * size, float(t.y) * size, size);   // empty tiles aren't in the atlas: skipped
        }
        d.draw.end(p, 1.0f);
        d.placed = tiles.revision(L);
    }
    if (d.draw.empty()) return;
    // The space's camera, with the content shifted by the offset.
    GpuUniform* u = uniform(p, m_cameras, p.cameraLayout, sizeof(Mat4::m));
    if (!u) return;
    const Mat4 viewProj = space.camera.viewProj(space.viewW, space.viewH, offset);
    u->write(p.ctx.queue, viewProj.m, sizeof(viewProj.m));
    wgpuRenderPassEncoderSetPipeline(pass, p.atlasPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, u->group, 0, nullptr);
    d.draw.draw(pass);
}

bool PreviewRenderer::execute(const PreviewSpace& space, const CanvasPipelines& p, WGPUCommandEncoder enc)
{
    const WGPUDevice device = p.ctx.device;
    const WGPUQueue  queue  = p.ctx.queue;
    const CanvasCamera& cam = space.camera;

    if (!m_empty) {
        if (!m_empty.create(device, { 1, 1, p.ctx.targetFormat, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst, "Preview Empty" }))
            return false;
        const uint32_t zero = 0;
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = m_empty.texture();
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow  = 4;
        layout.rowsPerImage = 1;
        const WGPUExtent3D size = { 1, 1, 1 };
        wgpuQueueWriteTexture(queue, &dst, &zero, 4, &layout, &size);
    }
    m_cameras.used = m_signalParams.used = m_strokeParams.used = 0;
    for (size_t i = 0; i < m_ops.size(); ++i)   // first: growing them drops every one
        if (!target(space, p, i)) return false;  // no memory for it: no picture (logged)
    const auto viewOf = [&](int op) { return op >= 0 ? m_targets[size_t(op)].view() : m_empty.view(); };

    for (size_t i = 0; i < m_ops.size(); ++i) {
        const Op& op = m_ops[i];
        LeafPass content;   // stays "none" unless this is a leaf pass
        if (op.kind == Op::Kind::Leaf) {
            content = { op.leaf, space.level, m_preview->leaf(op.leaf).revision(space.level), op.offset.x, op.offset.y,
                        cam.pan.x, cam.pan.y, cam.zoom, cam.rotation, space.pixelW, space.pixelH };
            if (m_content[i] == content) continue;   // its texture already shows exactly this
        }
        m_content[i] = content;

        const GpuRenderPass pass(enc, m_targets[i].view(), GpuRenderPass::Load::Clear, "Preview Pass");
        pass.confine(0, 0, space.pixelW, space.pixelH);   // targets may be bigger than the picture: drawn from their corner
        switch (op.kind) {
        case Op::Kind::Leaf:
            drawLeaf(pass, op.leaf, op.offset, space, p);
            break;
        case Op::Kind::Stroke: {
            const BrushParams& brush = m_preview->project().stroke().params();
            const RGBA c = brush.color;   // straight
            const float a = float(c.a) / 255.0f;
            PreviewStrokeParams params{};
            params.color[0]  = float(c.r) / 255.0f * a;
            params.color[1]  = float(c.g) / 255.0f * a;
            params.color[2]  = float(c.b) / 255.0f * a;
            params.color[3]  = a;
            params.opacity   = float(brush.opacity) / 255.0f;
            params.erase     = brush.mode == CompositeMode::Erase;
            params.keepAlpha = brush.keepAlpha;
            GpuUniform* u = uniform(p, m_strokeParams, p.previewStrokeLayout, sizeof(params));
            if (!u) break;
            u->write(queue, &params, sizeof(params));
            const WGPUTextureView views[2] = { viewOf(op.pixels), viewOf(op.coverage) };
            const GpuObject<WGPUBindGroup> textures = textureBindGroup(device, p.previewStrokeTexturesLayout, views, "Preview Stroke");   // the recorded pass keeps it alive
            wgpuRenderPassEncoderSetPipeline(pass, p.previewStrokePipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, u->group, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, textures, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            break;
        }
        case Op::Kind::Stack:
            for (const Layer& l : op.layers) {
                if (l.leaf >= 0) {
                    drawLeaf(pass, l.leaf, l.offset, space, p);
                    continue;
                }
                if (l.op < 0) continue;
                const WGPUTextureView v = viewOf(l.op);
                const GpuObject<WGPUBindGroup> g = textureBindGroup(device, p.blitLayout, { &v, 1 }, "Preview Layer");
                wgpuRenderPassEncoderSetPipeline(pass, p.blitPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, g, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            }
            break;
        case Op::Kind::Signal: {
            const Node& n = *m_preview->plan().nodes()[size_t(op.node)].node;
            PreviewSignalParams params{};
            for (int c = 0; c < kChannelCount; ++c) {
                const auto slot = [&](NodeSignal s) { return op.masks[maskSlot(s, NodeChannel(c))] >= 0 ? maskSlot(s, NodeChannel(c)) : -1; };
                params.mainK[c] = m_preview->scale(n, NodeSignal::Main, NodeChannel(c));
                params.wetK[c]  = n.hasDryWet() ? m_preview->scale(n, NodeSignal::Wet, NodeChannel(c)) : 1.0f;
                params.dryK[c]  = n.hasDryWet() ? m_preview->scale(n, NodeSignal::Dry, NodeChannel(c)) : 1.0f;
                params.mainM[c] = slot(NodeSignal::Main);
                params.wetM[c]  = slot(NodeSignal::Wet);
                params.dryM[c]  = slot(NodeSignal::Dry);
            }
            params.flags[0] = op.wet >= 0;
            params.flags[1] = op.dry >= 0;
            GpuUniform* u = uniform(p, m_signalParams, p.previewParamsLayout, sizeof(params));
            if (!u) break;
            u->write(queue, &params, sizeof(params));

            WGPUTextureView views[2 + kPreviewMaskTextures];
            views[0] = viewOf(op.wet);
            views[1] = viewOf(op.dry);
            for (int m = 0; m < kPreviewMaskTextures; ++m) views[2 + m] = viewOf(op.masks[m]);
            const GpuObject<WGPUBindGroup> textures = textureBindGroup(device, p.previewTexturesLayout, views, "Preview Signal");
            wgpuRenderPassEncoderSetPipeline(pass, p.previewSignalPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, u->group, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, textures, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            break;
        }
        }
    }
    return true;
}

WGPUTextureView PreviewRenderer::result() const
{
    return m_hasResult && m_result < m_targets.size() ? m_targets[m_result].view() : nullptr;
}

void PreviewRenderer::forget()
{
    m_ops.clear();
    m_built.clear();
    m_draws.clear();
    m_content.clear();   // the textures stay; what they show is no longer known
    m_rendered  = {};
    m_hasResult = false;
    m_ready     = false;
    m_target    = -1;
}

void PreviewRenderer::release()
{
    forget();
    m_targets.clear();
    m_targetW = m_targetH = 0;
    for (UniformPool* pool : { &m_cameras, &m_signalParams, &m_strokeParams }) {
        for (GpuUniform& u : pool->items) u.release();
        pool->items.clear();
        pool->used = 0;
    }
    m_empty.reset();
}

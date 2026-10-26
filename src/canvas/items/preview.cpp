#include "preview.hpp"
#include "gfx/math.hpp"

#include <algorithm>
#include <cmath>

namespace {

int maskSlot(NodeSignal s, NodeChannel c) { return int(s) * kChannelCount + int(c); }

}

// ---------------------------------------------------------------- plan

int EditPreviewItem::leafFor(const NodePtr& n, bool wetOnly)
{
    auto [it, inserted] = m_leafIndex[wetOnly].try_emplace(n.get(), int(m_leaves.size()));
    if (!inserted) return it->second;
    auto leaf = std::make_unique<Leaf>();
    if (wetOnly) {
        TileSet footprint;
        n->wetFootprint(footprint);
        leaf->pyramid.reset([this, n](int tx, int ty) { m_render.beginTile(); return n->fetchWet(tx, ty, m_render); }, std::move(footprint));
    } else {
        leaf->pyramid.reset([this, n](int tx, int ty) { return renderTile(*n, tx, ty, m_render); }, n->footprint());
    }
    m_leaves.push_back(std::move(leaf));
    return it->second;
}

int EditPreviewItem::stackLeaf(std::vector<NodePtr> layers)
{
    if (layers.empty()) return -1;
    auto leaf = std::make_unique<Leaf>();
    TileSet footprint;
    for (const NodePtr& l : layers) l->addFootprint(footprint);
    leaf->pyramid.reset([this, layers = std::move(layers)](int tx, int ty) {
        m_render.beginTile();
        return composeStack(layers.data(), layers.data() + layers.size(), tx, ty, m_render);
    }, std::move(footprint));
    m_leaves.push_back(std::move(leaf));
    return int(m_leaves.size()) - 1;
}

EditPreviewItem::Source EditPreviewItem::sourceFor(const NodePtr& n)
{
    if (m_reached.contains(n.get())) return { -1, planFor(n) };
    return { leafFor(n, false), -1 };
}

int EditPreviewItem::planFor(const NodePtr& n)
{
    if (auto it = m_planIndex.find(n.get()); it != m_planIndex.end()) return it->second;
    Plan p;
    p.node = n;
    auto* stack = dynamic_cast<CompositorNode*>(n.get());
    const bool recompose = stack && n->m_meta.id != m_node &&
        std::any_of(stack->layers().begin(), stack->layers().end(), [&](const NodePtr& l) { return m_reached.contains(l.get()); });
    if (auto* u = dynamic_cast<UnaryNode*>(n.get())) {
        if (u->input()) p.input = sourceFor(u->input());
    } else if (recompose) {
        // Layers the edit reaches are rebuilt; the runs between them are frozen as one leaf each.
        std::vector<NodePtr> run;
        const auto flush = [&] { if (!run.empty()) p.layers.push_back({ stackLeaf(std::move(run)), -1 }); run.clear(); };
        for (const NodePtr& l : stack->layers()) {
            if (!m_reached.contains(l.get())) { run.push_back(l); continue; }
            flush();
            p.layers.push_back(sourceFor(l));
        }
        flush();
    } else {
        p.wetLeaf = leafFor(n, true);   // its wet can't change: frozen, its signals change on the GPU
    }
    for (int sig = 0; sig < kSignalCount; ++sig)
        for (int ch = 0; ch < kChannelCount; ++ch)
            if (const NodePtr& m = n->signal(NodeSignal(sig)).mask[ch]; m && n->hasSignal(NodeSignal(sig)))
                p.masks[sig][ch] = sourceFor(m);
    m_plans.push_back(std::move(p));
    m_planIndex[n.get()] = int(m_plans.size()) - 1;
    return int(m_plans.size()) - 1;
}

bool EditPreviewItem::begin()
{
    end();
    const EditPreview& pv = m_project.preview();
    NodePtr edited = m_project.findPtr(pv.node);
    if (!edited || !pv.active()) return false;
    const NodePtr master = m_project.findPtr(m_project.master().m_meta.id);

    // Everything downstream of the edit, through inputs and masks.
    m_reached.insert(edited.get());
    for (bool grew = true; grew;) {
        grew = false;
        for (const NodePtr& n : m_project.nodes()) {
            if (m_reached.contains(n.get())) continue;
            for (const Node* r : m_reached)
                if (n->usesAsInput(r) || n->usesAsMask(r)) { m_reached.insert(n.get()); grew = true; break; }
        }
    }
    if (!master || !m_reached.contains(master.get())) { end(); return false; }

    m_node       = pv.node;
    m_masterPlan = planFor(master);
    m_active     = true;
    update();
    return true;
}

void EditPreviewItem::end()
{
    m_leaves.clear();
    m_leafIndex[0].clear();
    m_leafIndex[1].clear();
    m_reached.clear();
    m_plans.clear();
    m_planIndex.clear();
    m_masterPlan = -1;
    m_ops.clear();
    m_built.clear();
    m_leavesReady = false;
    m_hasResult   = false;
    m_node        = kNoNodeId;
    m_rendered    = {};
    m_targetContent.clear();   // the textures stay; what they show is no longer known
    if (m_active) update();
    m_active = false;
}

// ---------------------------------------------------------------- per frame

float EditPreviewItem::scale(const Node& n, NodeSignal s, NodeChannel c) const
{
    const EditPreview& pv = m_project.preview();
    return pv.isScale(n.m_meta.id, s, c) ? pv.scale : n.signal(s).scale[int(c)];
}

int EditPreviewItem::leafOp(int leaf, Vec2 offset)
{
    if (leaf < 0) return -1;
    for (const Built& b : m_built)
        if (b.leaf == leaf && b.offset.x == offset.x && b.offset.y == offset.y) return b.op;
    Op op{};
    op.kind   = Op::Kind::Leaf;
    op.leaf   = leaf;
    op.offset = offset;
    m_ops.push_back(op);
    m_built.push_back({ -1, leaf, offset, int(m_ops.size()) - 1 });
    return int(m_ops.size()) - 1;
}

int EditPreviewItem::opFor(Source src, Vec2 offset)
{
    return src.plan >= 0 ? build(src.plan, offset) : leafOp(src.leaf, offset);
}

int EditPreviewItem::signalOp(int plan, int wet, int dry, Vec2 offset)
{
    Op op{};
    op.kind = Op::Kind::Signal;
    op.plan = plan;
    op.wet  = wet;
    op.dry  = dry;
    for (int i = 0; i < kPreviewMaskTextures; ++i) op.masks[i] = -1;
    for (int sig = 0; sig < kSignalCount; ++sig)
        for (int ch = 0; ch < kChannelCount; ++ch)
            op.masks[maskSlot(NodeSignal(sig), NodeChannel(ch))] = opFor(m_plans[size_t(plan)].masks[sig][ch], offset);
    m_ops.push_back(op);
    return int(m_ops.size()) - 1;
}

int EditPreviewItem::build(int plan, Vec2 offset)
{
    for (const Built& b : m_built)   // reached twice: built once per offset, shared
        if (b.plan == plan && b.offset.x == offset.x && b.offset.y == offset.y) return b.op;

    const Plan& s = m_plans[size_t(plan)];
    Node& n = *s.node;
    const auto on = [&](NodeSignal sig) { return Node::gainByte(scale(n, sig, NodeChannel::A)) != 0; };

    int wet = -1, dry = -1;
    if (!n.hasDryWet() || on(NodeSignal::Wet)) {
        if (auto* m = dynamic_cast<MoveNode*>(&n)) {
            const EditPreview& pv = m_project.preview();
            const bool moving = pv.kind == EditPreview::Kind::MoveOffset && pv.node == n.m_meta.id;
            const Vec2 shift = { float(moving ? pv.x : m->offsetX()), float(moving ? pv.y : m->offsetY()) };
            wet = opFor(s.input, { offset.x + shift.x, offset.y + shift.y });   // a move moves its input
        } else if (dynamic_cast<UnaryNode*>(&n)) {
            wet = opFor(s.input, offset);
        } else if (!s.layers.empty()) {   // a stack the edit reaches: recomposed
            std::vector<int> layers;
            for (Source l : s.layers) layers.push_back(opFor(l, offset));
            Op stack{};
            stack.kind   = Op::Kind::Stack;
            stack.layers = std::move(layers);
            m_ops.push_back(std::move(stack));
            wet = int(m_ops.size()) - 1;
        } else {
            wet = leafOp(s.wetLeaf, offset);
        }
    }
    if (n.hasDryWet() && on(NodeSignal::Dry)) dry = opFor(s.input, offset);
    const int op = signalOp(plan, wet, dry, offset);
    m_built.push_back({ plan, -1, offset, op });
    return op;
}

TileRect EditPreviewItem::leafRange(const CanvasFrame& frame, const Op& op) const
{
    // The leaf's tiles that land on screen once shifted by the op's offset.
    using TileUtils::worldToTileClamped;
    return { worldToTileClamped(frame.view.minX - op.offset.x) >> m_level, worldToTileClamped(frame.view.minY - op.offset.y) >> m_level,
             worldToTileClamped(frame.view.maxX - op.offset.x) >> m_level, worldToTileClamped(frame.view.maxY - op.offset.y) >> m_level };
}

EditPreviewItem::FrameKey EditPreviewItem::frameKey(const CanvasFrame& frame) const
{
    const EditPreview& pv = m_project.preview();
    FrameKey k;
    k.kind    = pv.kind;
    k.node    = pv.node;
    k.signal  = int(pv.signal);
    k.channel = int(pv.channel);
    k.x       = pv.x;
    k.y       = pv.y;
    k.scale   = pv.scale;
    k.panX    = frame.camera.pan.x;
    k.panY    = frame.camera.pan.y;
    k.zoom    = frame.camera.zoom;
    k.rotation = frame.camera.rotation;
    k.width   = frame.target.pixelW;
    k.height  = frame.target.pixelH;
    k.level   = m_level;
    for (const auto& leaf : m_leaves) k.revisions += leaf->levels[m_level].revision;
    return k;
}

bool EditPreviewItem::catchUp(CanvasFrame& frame)
{
    bool ready = true;
    for (const Op& op : m_ops) {
        if (op.kind != Op::Kind::Leaf) continue;
        Leaf& leaf = *m_leaves[size_t(op.leaf)];
        Leaf::Level& lv = leaf.levels[m_level];
        forEachTileIn(leaf.pyramid.footprint(m_level), leafRange(frame, op), [&](TileKey key) {
            if (lv.done.contains(key)) return true;
            if (!frame.inBudget()) { ready = false; return false; }
            const auto t = TileUtils::unpackKey(key);
            bool complete = true;
            const Tile* tile = leaf.pyramid.tile(frame, m_level, t.x, t.y, complete);
            if (!complete) { ready = false; return false; }
            if (tile) lv.atlas.upload(frame.pipelines, key, tile->getReadAll());
            lv.done.insert(key);
            ++lv.revision;
            return true;
        });
    }
    return ready;
}

bool EditPreviewItem::prepare(CanvasFrame& frame)
{
    if (!m_active) return false;
    // The drag ended and the picture already shows the value: kept until the output catches up.
    if (!m_project.preview().active() && m_hasResult) return false;

    m_level = frame.lodLevel(TilePyramid::kLevels);

    m_ops.clear();
    m_built.clear();
    build(m_masterPlan, {});   // the master's output: the last op
    m_leavesReady = catchUp(frame);
    if (!m_leavesReady) return false;   // the view keeps its picture until every input is there

    FrameKey key = frameKey(frame);
    if (m_hasResult && key == m_rendered) return false;   // the result already shows exactly this
    execute(frame);
    m_rendered  = key;
    m_hasResult = true;
    return true;
}

// ---------------------------------------------------------------- GPU

EditPreviewItem::Target& EditPreviewItem::target(const CanvasFrame& frame, size_t i)
{
    if (frame.target.pixelW != m_targetW || frame.target.pixelH != m_targetH) {
        releaseTargets();
        m_targetW = frame.target.pixelW;
        m_targetH = frame.target.pixelH;
    }
    m_targetContent.resize(std::max(m_targetContent.size(), i + 1));
    while (m_targets.size() <= i) {
        Target t;
        WGPUTextureDescriptor td{};
        td.label         = sv("Edit Preview Target");
        td.dimension     = WGPUTextureDimension_2D;
        td.size          = { m_targetW, m_targetH, 1 };
        td.format        = frame.pipelines.ctx.targetFormat;
        td.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        t.texture = wgpuDeviceCreateTexture(frame.pipelines.ctx.device, &td);
        t.view    = wgpuTextureCreateView(t.texture, nullptr);
        m_targets.push_back(t);
    }
    return m_targets[i];
}

GpuUniform& EditPreviewItem::uniform(const CanvasFrame& frame, UniformPool& pool, WGPUBindGroupLayout layout, uint64_t size)
{
    if (pool.used == pool.items.size()) pool.items.emplace_back().create(frame.pipelines.ctx.device, layout, size, "Edit Preview Uniform");
    return pool.items[pool.used++];
}

namespace {

WGPURenderPassEncoder beginPass(WGPUCommandEncoder enc, WGPUTextureView view)
{
    WGPURenderPassColorAttachment ca{};
    ca.view       = view;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp     = WGPULoadOp_Clear;
    ca.storeOp    = WGPUStoreOp_Store;
    ca.clearValue = { 0.0, 0.0, 0.0, 0.0 };
    WGPURenderPassDescriptor rp{};
    rp.label                = sv("Edit Preview Pass");
    rp.colorAttachmentCount = 1;
    rp.colorAttachments     = &ca;
    return wgpuCommandEncoderBeginRenderPass(enc, &rp);
}

void endPass(WGPURenderPassEncoder pass)
{
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

WGPUBindGroup textureGroup(WGPUDevice device, WGPUBindGroupLayout layout, const WGPUTextureView* views, size_t count)
{
    std::vector<WGPUBindGroupEntry> e(count);
    for (size_t i = 0; i < count; ++i) {
        e[i].binding     = uint32_t(i);
        e[i].textureView = views[i];
    }
    WGPUBindGroupDescriptor gd{};
    gd.label      = sv("Edit Preview Textures");
    gd.layout     = layout;
    gd.entryCount = count;
    gd.entries    = e.data();
    return wgpuDeviceCreateBindGroup(device, &gd);
}

}

void EditPreviewItem::execute(CanvasFrame& frame)
{
    const CanvasPipelines& p = frame.pipelines;
    const WGPUDevice device = p.ctx.device;
    const WGPUQueue  queue  = p.ctx.queue;
    const CanvasCamera& cam = frame.camera;
    WGPUCommandEncoder enc  = frame.target.encoder;

    if (!m_dummy.texture) {
        WGPUTextureDescriptor td{};
        td.label         = sv("Edit Preview Empty");
        td.dimension     = WGPUTextureDimension_2D;
        td.size          = { 1, 1, 1 };
        td.format        = p.ctx.targetFormat;
        td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        m_dummy.texture = wgpuDeviceCreateTexture(device, &td);
        m_dummy.view    = wgpuTextureCreateView(m_dummy.texture, nullptr);
        const uint32_t zero = 0;
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = m_dummy.texture;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow  = 4;
        layout.rowsPerImage = 1;
        const WGPUExtent3D size = { 1, 1, 1 };
        wgpuQueueWriteTexture(queue, &dst, &zero, 4, &layout, &size);
    }
    m_cameras.used = m_params.used = 0;
    const auto viewOf = [&](int op) { return op >= 0 ? target(frame, size_t(op)).view : m_dummy.view; };

    for (size_t i = 0; i < m_ops.size(); ++i) {
        const Op& op = m_ops[i];
        const WGPUTextureView out = target(frame, i).view;

        LeafPass content;   // stays "none" unless this is a leaf pass
        if (op.kind == Op::Kind::Leaf) {
            content = { op.leaf, m_level, m_leaves[size_t(op.leaf)]->levels[m_level].revision,
                        op.offset.x, op.offset.y, cam.pan.x, cam.pan.y, cam.zoom, cam.rotation };
            if (m_targetContent[i] == content) continue;   // its texture already shows exactly this
        }
        m_targetContent[i] = content;

        WGPURenderPassEncoder pass = beginPass(enc, out);

        switch (op.kind) {
        case Op::Kind::Leaf: {
            Leaf::Level& lv = m_leaves[size_t(op.leaf)]->levels[m_level];
            if (lv.atlas.empty()) break;
            // Every uploaded tile (all near the view; the GPU clips the rest), one list for every
            // offset this frame: buffer writes land before the passes run, so per-pass lists clash.
            if (lv.placed != lv.revision) {
                const float size = float(Tile::kSize << m_level);
                lv.atlas.beginInstances();
                for (TileKey key : lv.done) {
                    const auto t = TileUtils::unpackKey(key);
                    lv.atlas.add(key, float(TileUtils::tileToWorld(t.x)) * float(1 << m_level),
                                 float(TileUtils::tileToWorld(t.y)) * float(1 << m_level), size);
                }
                lv.atlas.endInstances(p, 1.0f);
                lv.placed = lv.revision;
            }
            // The view's camera, with the content shifted by the op's offset.
            GpuUniform& u = uniform(frame, m_cameras, p.cameraLayout, sizeof(Mat4::m));
            const Mat4 viewProj = cam.viewProj(frame.target.viewW, frame.target.viewH, op.offset);
            u.write(queue, viewProj.m, sizeof(viewProj.m));
            wgpuRenderPassEncoderSetPipeline(pass, p.atlasPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, u.group, 0, nullptr);
            lv.atlas.draw(pass);
            break;
        }
        case Op::Kind::Signal: {
            const Node& n = *m_plans[size_t(op.plan)].node;
            PreviewSignalParams params{};
            for (int c = 0; c < kChannelCount; ++c) {
                params.mainK[c] = scale(n, NodeSignal::Main, NodeChannel(c));
                params.wetK[c]  = n.hasDryWet() ? scale(n, NodeSignal::Wet, NodeChannel(c)) : 1.0f;
                params.dryK[c]  = n.hasDryWet() ? scale(n, NodeSignal::Dry, NodeChannel(c)) : 1.0f;
                params.mainM[c] = op.masks[maskSlot(NodeSignal::Main, NodeChannel(c))] >= 0 ? maskSlot(NodeSignal::Main, NodeChannel(c)) : -1;
                params.wetM[c]  = op.masks[maskSlot(NodeSignal::Wet,  NodeChannel(c))] >= 0 ? maskSlot(NodeSignal::Wet,  NodeChannel(c)) : -1;
                params.dryM[c]  = op.masks[maskSlot(NodeSignal::Dry,  NodeChannel(c))] >= 0 ? maskSlot(NodeSignal::Dry,  NodeChannel(c)) : -1;
            }
            params.flags[0] = op.wet >= 0;
            params.flags[1] = op.dry >= 0;
            GpuUniform& u = uniform(frame, m_params, p.previewParamsLayout, sizeof(params));
            u.write(queue, &params, sizeof(params));

            WGPUTextureView views[2 + kPreviewMaskTextures];
            views[0] = viewOf(op.wet);
            views[1] = viewOf(op.dry);
            for (int m = 0; m < kPreviewMaskTextures; ++m) views[2 + m] = viewOf(op.masks[m]);
            WGPUBindGroup textures = textureGroup(device, p.previewTexturesLayout, views, 2 + kPreviewMaskTextures);
            wgpuRenderPassEncoderSetPipeline(pass, p.previewSignalPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, u.group, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, textures, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            wgpuBindGroupRelease(textures);   // the recorded pass keeps it alive
            break;
        }
        case Op::Kind::Stack:
            wgpuRenderPassEncoderSetPipeline(pass, p.blitPipeline);
            for (int layer : op.layers) {
                if (layer < 0) continue;
                const WGPUTextureView v = viewOf(layer);
                WGPUBindGroup g = textureGroup(device, p.blitLayout, &v, 1);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, g, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
                wgpuBindGroupRelease(g);
            }
            break;
        }
        endPass(pass);
    }

    if (m_resultGroup) wgpuBindGroupRelease(m_resultGroup);
    const WGPUTextureView result = target(frame, m_ops.size() - 1).view;
    m_resultGroup = textureGroup(device, p.blitLayout, &result, 1);
}

void EditPreviewItem::drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame)
{
    if (!m_active || !m_hasResult || !m_resultGroup) return;
    frame.bindOwn(pass, frame.pipelines.blitPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_resultGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
}

void EditPreviewItem::releaseTargets()
{
    if (m_resultGroup) { wgpuBindGroupRelease(m_resultGroup); m_resultGroup = nullptr; }
    for (Target& t : m_targets) {
        if (t.view)    wgpuTextureViewRelease(t.view);
        if (t.texture) wgpuTextureRelease(t.texture);
    }
    m_targets.clear();
    m_targetContent.clear();
    m_hasResult = false;
}

void EditPreviewItem::releaseGpu()
{
    end();
    releaseTargets();
    for (UniformPool* pool : { &m_cameras, &m_params }) {
        for (GpuUniform& u : pool->items) u.release();
        pool->items.clear();
        pool->used = 0;
    }
    if (m_dummy.view)    wgpuTextureViewRelease(m_dummy.view);
    if (m_dummy.texture) wgpuTextureRelease(m_dummy.texture);
    m_dummy = {};
}

#include "canvas-view.h"
#include "core/perf.h"

#include <cmath>

bool CanvasView::init(CanvasScene& scene)
{
    m_scene = &scene;
    const CanvasPipelines& p = scene.pipelines();
    m_target.init(p.ctx.device, p.ctx.targetFormat);
    return m_camera.create(p.ctx, p.cameraLayout);
}

void CanvasView::shutdown()
{
    m_camera.release();
    m_target.shutdown();
}

bool CanvasView::resize(float width, float height, float pixelScaleX, float pixelScaleY)
{
    const uint32_t pw = (uint32_t)std::lround(width * pixelScaleX);
    const uint32_t ph = (uint32_t)std::lround(height * pixelScaleY);
    if (pw == 0 || ph == 0) return false;

    const uint32_t oldW = m_target.width(), oldH = m_target.height();
    if (!m_target.ensure(pw, ph)) return false;
    if (m_target.width() != oldW || m_target.height() != oldH) m_targetNew = true;   // a new texture starts empty
    m_width  = width;
    m_height = height;
    return true;
}

void CanvasView::render(WGPUCommandEncoder enc, double budgetMs)
{
    if (!m_scene || !m_target.view() || m_width <= 0.0f || m_height <= 0.0f) return;
    PerfScope perf(Perf::CanvasPrepare);

    CanvasFrame frame(m_scene->pipelines(), m_camera, visibleWorld(), { enc, m_width, m_height, m_target.width(), m_target.height() }, budgetMs);
    const bool itemsChanged = m_scene->prepare(frame);
    const bool viewChanged  = m_targetNew || m_camera.pan.x != m_renderedPan.x || m_camera.pan.y != m_renderedPan.y ||
                              m_camera.zoom != m_renderedZoom || m_width != m_renderedW || m_height != m_renderedH;
    const bool contentChanged = itemsChanged || m_scene->revision() != m_renderedRevision;
    if (!viewChanged && !contentChanged && !m_holding) return;

    // Hold the last complete picture until the visible content has fully caught up.
    if (!viewChanged && m_holdAllowed && !m_scene->settled()) {
        const auto now = std::chrono::steady_clock::now();
        if (!m_holding) { m_holding = true; m_holdStart = now; }
        if (std::chrono::duration<double>(now - m_holdStart).count() < kMaxHoldSeconds) return;
    }
    m_holding = false;

    m_camera.upload(m_width, m_height);

    WGPURenderPassColorAttachment ca{};
    ca.view       = m_target.view();
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp     = WGPULoadOp_Clear;
    ca.storeOp    = WGPUStoreOp_Store;
    ca.clearValue = { clearColor[0], clearColor[1], clearColor[2], clearColor[3] };

    WGPURenderPassDescriptor rp{};
    rp.label                = sv("Canvas Pass");
    rp.colorAttachmentCount = 1;
    rp.colorAttachments     = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    m_scene->draw(pass, frame);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    m_renderedRevision = m_scene->revision();
    m_renderedPan  = m_camera.pan;
    m_renderedZoom = m_camera.zoom;
    m_renderedW    = m_width;
    m_renderedH    = m_height;
    m_targetNew    = false;
}

void CanvasView::paintOverlays(ImDrawList* dl, float originX, float originY)
{
    if (m_scene) m_scene->paintOverlays(dl, OverlayContext{ m_camera, originX, originY, m_width, m_height });
}

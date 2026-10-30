#include "view.hpp"
#include "base/palette.hpp"
#include "base/perf.hpp"
#include "gfx/render-pass.hpp"

#include <cmath>

bool CanvasView::init(CanvasScene& scene)
{
    m_scene = &scene;
    const CanvasPipelines& p = scene.pipelines();
    m_target.init(p.ctx.device, p.ctx.targetFormat);
    m_queue = p.ctx.queue;
    return m_cameraUniform.create(p.ctx.device, p.cameraLayout, sizeof(Mat4::m), "Canvas Camera");
}

void CanvasView::shutdown()
{
    m_cameraUniform.release();
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

void CanvasView::render(WGPUCommandEncoder enc, const Deadline& budget)
{
    if (!m_scene || !m_target.view() || m_width <= 0.0f || m_height <= 0.0f) return;
    PerfScope perf(Perf::CanvasPrepare);

    CanvasFrame frame(m_scene->pipelines(), m_camera, m_cameraUniform.group, visibleWorld(),
                      { enc, m_width, m_height, m_target.width(), m_target.height() }, budget);
    const bool itemsChanged = m_scene->prepare(frame);
    const bool viewChanged  = m_targetNew || m_camera.pan.x != m_renderedPan.x || m_camera.pan.y != m_renderedPan.y ||
                              m_camera.zoom != m_renderedZoom || m_camera.rotation != m_renderedRotation || m_width != m_renderedW || m_height != m_renderedH;
    const bool contentChanged = itemsChanged || m_scene->revision() != m_renderedRevision;
    if (!viewChanged && !contentChanged && !m_holding) return;

    // Hold the last complete picture until the visible content has fully caught up.
    if (!viewChanged && !m_scene->settled()) {
        if (!m_holding) { m_holding = true; m_holdEnd = Deadline::in(kMaxHoldMs); }
        if (!m_holdEnd.expired()) return;
    }
    m_holding = false;

    const Mat4 viewProj = m_camera.viewProj(m_width, m_height);
    m_cameraUniform.write(m_queue, viewProj.m, sizeof(viewProj.m));

    {
        const auto clear = Palette::kCanvasBackground.floats();
        const GpuRenderPass pass(enc, m_target.view(), GpuRenderPass::Load::Clear, "Canvas Pass", clear.data());
        m_scene->drawGpu(pass, frame);
    }

    m_renderedRevision = m_scene->revision();
    m_renderedPan  = m_camera.pan;
    m_renderedZoom = m_camera.zoom;
    m_renderedRotation = m_camera.rotation;
    m_renderedW    = m_width;
    m_renderedH    = m_height;
    m_targetNew    = false;
}

void CanvasView::drawOverlays(ImDrawList* dl, float originX, float originY)
{
    if (m_scene) m_scene->drawOverlays(dl, OverlayContext{ m_camera, originX, originY, m_width, m_height });
}

#include "checker.hpp"

#include <cmath>
#include <cstring>

void CheckerItem::setCellScale(float scale)
{
    if (scale == m_cellScale || scale <= 0.0f) return;
    m_cellScale = scale;
    m_dirty = true;
    update();
}

bool CheckerItem::prepare(CanvasFrame& frame)
{
    const CanvasPipelines& p = frame.pipelines;
    if (!m_uniform) {
        if (!m_uniform.create(p.ctx.device, p.checkerLayout, sizeof(CheckerParams), "Checker")) return false;   // logged; no checker
        m_dirty = true;
    }

    // In target pixels: the origin moves with the camera, the square size is fixed on screen.
    const float pixelsPerPoint = frame.pixelsPerPoint();
    CheckerParams u{};
    const Style::CanvasStyle& style = Style::canvas();
    const bool canvas = m_kind == Kind::Canvas;
    const auto a = (canvas ? style.checkerA : style.pictureCheckerA).floats();
    const auto b = (canvas ? style.checkerB : style.pictureCheckerB).floats();
    const float cell = (canvas ? style.checkerCell : style.pictureCheckerCell) * m_cellScale;
    for (size_t i = 0; i < 3; ++i) { u.colorA[i] = a[i]; u.colorB[i] = b[i]; }
    u.colorA[3] = u.colorB[3] = 1.0f;
    u.origin[0] = frame.camera.pan.x * pixelsPerPoint;
    u.origin[1] = frame.camera.pan.y * pixelsPerPoint;
    u.cell      = std::max(1.0f, std::round(cell * pixelsPerPoint));
    if (!m_dirty && std::memcmp(&u, &m_uploaded, sizeof u) == 0) return false;
    m_uniform.write(p.ctx.queue, &u, sizeof u);
    m_uploaded = u;
    const bool own = m_dirty;   // a camera move re-renders the view anyway
    m_dirty = false;
    return own;
}

void CheckerItem::drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame)
{
    if (!m_uniform) return;
    frame.bindOwn(pass, frame.pipelines.checkerPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_uniform.group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
}

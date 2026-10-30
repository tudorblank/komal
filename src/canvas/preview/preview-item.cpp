#include "canvas/preview/preview-item.hpp"
#include "canvas/preview/preview.hpp"
#include "document/project.hpp"
#include "gfx/render-pass.hpp"

bool PreviewItem::prepare(CanvasFrame& frame)
{
    const bool wasShowing = m_showing;
    m_planned = m_preview->active() && m_renderer.plan(*m_preview, m_preview->project().master().id());
    bool changed = false;
    if (m_planned)
        changed = m_renderer.render(*m_preview, PreviewSpace::ofView(frame), frame.pipelines, frame.target.encoder, frame.budget());
    m_showing = m_planned && m_renderer.hasResult();
    if (m_showing && m_renderer.result() != m_groupView) {
        m_groupView   = m_renderer.result();
        m_resultGroup = textureBindGroup(frame.pipelines.ctx.device, frame.pipelines.blitLayout, { &m_groupView, 1 }, "Preview Result");
    }
    m_output->setSuppressed(m_showing);   // before anything draws this frame
    return changed || m_showing != wasShowing;
}

void PreviewItem::drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame)
{
    if (!m_showing || !m_resultGroup) return;
    frame.bindOwn(pass, frame.pipelines.blitPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_resultGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
}

void PreviewItem::releaseGpu()
{
    m_resultGroup.reset();
    m_groupView = nullptr;
    m_renderer.release();
    m_showing = false;
}

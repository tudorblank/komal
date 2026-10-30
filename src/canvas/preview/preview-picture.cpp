#include "canvas/preview/preview-picture.hpp"
#include "canvas/preview/preview.hpp"
#include "gfx/render-pass.hpp"

#include <algorithm>
#include <cmath>

PreviewPicture::Result PreviewPicture::render(const Preview& preview, NodeId node, uint32_t maxW, uint32_t maxH, const CanvasPipelines& p,
                                              WGPUCommandEncoder enc, const Deadline& budget)
{
    m_retired.reset();   // a frame later: nothing draws it any more
    if (maxW == 0 || maxH == 0 || !m_renderer.plan(preview, node)) return Result::NotReached;
    const BoundsI b = m_renderer.bounds();
    if (b.empty()) return Result::Empty;

    const PictureFit fit = fitInto(b, maxW, maxH);
    const uint32_t w = fit.width, h = fit.height;
    const PreviewSpace space = PreviewSpace::fitted(b, w, h);
    if (!m_renderer.render(preview, space, p, enc, budget))
        return m_renderer.ready() && m_has ? Result::Unchanged : Result::Waiting;

    const GpuHandles& ctx = p.ctx;
    if (m_texture.width() < maxW || m_texture.height() < maxH) {
        const uint32_t tw = std::max(maxW, m_texture.width()), th = std::max(maxH, m_texture.height());
        m_retired = std::move(m_texture);
        if (!m_texture.create(ctx.device, { tw, th, ctx.targetFormat,
                                            WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc,
                                            "Preview Picture" })) {
            m_has = false;
            return Result::NotReached;   // no memory for it (logged): nothing to show
        }
    }
    if (!m_checker) {
        if (!m_checker.create(ctx.device, p.checkerLayout, sizeof(CheckerParams), "Preview Picture Checker")) return Result::NotReached;
        const CheckerParams c = pictureChecker(0.0f, 0.0f);   // from the picture's corner
        m_checker.write(ctx.queue, &c, sizeof c);
    }
    if (m_renderer.result() != m_groupView) {
        m_groupView   = m_renderer.result();
        m_resultGroup = textureBindGroup(ctx.device, p.blitLayout, { &m_groupView, 1 }, "Preview Picture Result");
    }

    {   // the checkerboard, the result over it
        const GpuRenderPass pass(enc, m_texture.view(), GpuRenderPass::Load::Clear, "Preview Picture Pass");
        pass.confine(0, 0, w, h);
        wgpuRenderPassEncoderSetPipeline(pass, p.checkerPipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_checker.group, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderSetPipeline(pass, p.blitPipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_resultGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    }

    m_w = w;
    m_h = h;
    m_bounds = b;
    m_has = true;
    return Result::Rendered;
}

void PreviewPicture::release()
{
    m_renderer.release();
    m_resultGroup.reset();
    m_groupView = nullptr;
    m_checker.release();
    m_texture.reset();
    m_retired.reset();
    m_has = false;
}

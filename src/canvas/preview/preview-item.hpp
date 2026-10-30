#pragma once
// The canvas branch of the preview (canvas/preview/preview.hpp): the master's output under the
// edit in progress, in a canvas view, rebuilt on the GPU every frame the edit moves. It takes the
// place of the view's output item: that one is suppressed the same frame it shows (and drawn again
// the frame it stops), so no frame shows both or neither. When the edit ends, the view holds its
// last picture until the output has caught up.

#include "canvas/item.hpp"
#include "canvas/items/raster.hpp"
#include "canvas/preview/preview-renderer.hpp"

class Preview;

class PreviewItem : public CanvasItem {
public:
    PreviewItem(const Preview& preview, RasterItem& output) : m_preview(&preview), m_output(&output) {}   // the scene's
    ~PreviewItem() override { releaseGpu(); }

    bool showing() const { return m_showing; }   // its picture takes the output's place

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override;
    bool settled() const override { return !m_planned || m_renderer.ready(); }

private:
    const Preview*  m_preview;
    RasterItem*     m_output;   // the master's output, which it replaces while showing
    PreviewRenderer m_renderer;
    GpuObject<WGPUBindGroup> m_resultGroup;   // the result, for the blit
    WGPUTextureView m_groupView = nullptr;    // ... made for this texture
    bool m_planned = false;   // the edit reaches the master
    bool m_showing = false;
};

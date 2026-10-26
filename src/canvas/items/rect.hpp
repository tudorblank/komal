#pragma once
// RectItem: a solid rectangle in world space, drawn on the GPU (color.wgsl) in the canvas pass,
// under or over other items by z. The building block for anything rectangular that belongs to
// the canvas picture itself (artboards, backgrounds); for crisp screen-space outlines see
// OutlineItem (outline.h).

#include "canvas/item.hpp"

// Straight-alpha colour.
class RectItem : public CanvasItem {
public:
    RectItem(float x, float y, float w, float h, float r, float g, float b, float a);
    ~RectItem() override { releaseGpu(); }

    void setRect(float x, float y, float w, float h);
    void setColor(float r, float g, float b, float a);

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override;

private:
    float m_x, m_y, m_w, m_h;
    float m_color[4];
    WGPUBuffer    m_buffer    = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    bool          m_upload    = true;   // uniform out of date
};

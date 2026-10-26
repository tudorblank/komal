#pragma once
// A solid rectangle in world space, drawn in the canvas pass by z (color.wgsl): part of the
// picture itself (artboards, backgrounds). Screen-space outlines are OutlineItem.

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
    void releaseGpu() override { m_uniform.release(); }

private:
    float m_x, m_y, m_w, m_h;
    float m_color[4];
    GpuUniform m_uniform;
    bool       m_upload = true;   // uniform out of date
};

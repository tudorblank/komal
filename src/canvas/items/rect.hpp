#pragma once
// A solid rectangle in world space, drawn in the canvas pass by z (color.wgsl): part of the
// picture itself (artboards, backgrounds). Screen-space outlines are OutlineItem.

#include "base/palette.hpp"
#include "canvas/item.hpp"

class RectItem : public CanvasItem {
public:
    RectItem(float x, float y, float w, float h, Color8 color);
    ~RectItem() override { releaseGpu(); }

    void setRect(float x, float y, float w, float h);
    void setColor(Color8 color);

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override { m_uniform.release(); }

private:
    float m_x, m_y, m_w, m_h;
    Color8 m_color;
    GpuUniform m_uniform;
    bool       m_upload = true;   // uniform out of date
};

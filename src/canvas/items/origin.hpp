#pragma once
// OriginItem: a thin "+" on the world origin (0, 0), the canvas's top-left corner. Two
// RectItems (color.wgsl) re-sized every frame so the lines stay one target pixel thick and the
// arms a fixed length on screen, whatever the zoom.

#include "canvas/items/rect.hpp"

class OriginItem : public CanvasItem {
public:
    explicit OriginItem(float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 0.45f);
    ~OriginItem() override { releaseGpu(); }

    void setColor(float r, float g, float b, float a);
    void setArm(float points);   // from the centre to each end, on screen

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override;

private:
    RectItem m_horizontal, m_vertical;   // not in the scene: drawn through this item
    float    m_arm = 8.0f;
};

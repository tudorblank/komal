#pragma once
// A thin "+" on the world origin: two RectItems re-sized every frame, so the lines stay one
// target pixel thick and the arms a fixed length on screen.

#include "canvas/items/rect.hpp"

class OriginItem : public CanvasItem {
public:
    explicit OriginItem(Color8 color = Palette::kOriginMark);
    ~OriginItem() override { releaseGpu(); }

    void setArm(float points);   // from the centre to each end, on screen

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override;

private:
    RectItem m_horizontal, m_vertical;   // not in the scene: drawn through this item
    float    m_arm = 8.0f;
};

#pragma once
// A thin "+" on the world origin: two RectItems re-sized every frame, so the lines stay one
// target pixel thick and the arms a fixed length on screen.

#include "ui/panels/canvas/items/rect.hpp"
#include "ui/style/ui-style.hpp"

class OriginItem : public CanvasItem {
public:
    OriginItem();   // in the canvas style's color, read as it draws
    ~OriginItem() override { releaseGpu(); }

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override;

private:
    RectItem m_horizontal, m_vertical;   // not in the scene: drawn through this item
    static constexpr float kArm = 8.0f;   // from the centre to each end, points on screen
};

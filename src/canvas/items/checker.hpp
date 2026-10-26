#pragma once
// The canvas background: a checkerboard where nothing covers it, drawn first over the whole
// view. Squares keep their size on screen at any zoom; the pattern is anchored to the canvas
// origin, so it travels with a pan (checker.wgsl).

#include "canvas/item.hpp"

class CheckerItem : public CanvasItem {
public:
    ~CheckerItem() override { releaseGpu(); }

    void setColors(const float a[3], const float b[3]);   // straight RGB 0..1; `a` is at the origin
    void setCellSize(float points);                       // in view points (scaled for HiDPI)

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override { m_uniform.release(); }

private:
    float m_colorA[3] = { 0x13 / 255.0f, 0x13 / 255.0f, 0x13 / 255.0f };   // #131313
    float m_colorB[3] = { 0x1F / 255.0f, 0x1F / 255.0f, 0x1F / 255.0f };   // #1F1F1F
    float m_cell = 8.0f;

    CheckerParams m_uploaded{};
    bool          m_dirty = true;
    GpuUniform    m_uniform;
};

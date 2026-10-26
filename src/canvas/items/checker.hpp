#pragma once
// CheckerItem (drawGpu): the canvas background, a checkerboard showing where there is nothing
// (transparency). Drawn first, under everything, over the whole view, so it never ends. Its
// squares are a fixed size on screen whatever the zoom, and the pattern is anchored to the
// canvas origin, so it travels with the canvas when panning (checker.wgsl).

#include "canvas/item.hpp"

class CheckerItem : public CanvasItem {
public:
    ~CheckerItem() override { releaseGpu(); }

    // Straight RGB 0..1; the square at the canvas origin is the first.
    void setColors(const float a[3], const float b[3]);
    void setCellSize(float points);   // square size, in view points (scaled to pixels for HiDPI)

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override;

private:
    float m_colorA[3] = { 0x13 / 255.0f, 0x13 / 255.0f, 0x13 / 255.0f };   // #131313
    float m_colorB[3] = { 0x1F / 255.0f, 0x1F / 255.0f, 0x1F / 255.0f };   // #1F1F1F
    float m_cell = 8.0f;

    CheckerParams m_uploaded{};   // what the uniform holds
    bool          m_dirty = true;
    WGPUBuffer    m_buffer = nullptr;
    WGPUBindGroup m_group  = nullptr;
};

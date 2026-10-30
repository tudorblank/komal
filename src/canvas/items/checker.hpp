#pragma once
// The canvas background: a checkerboard where nothing covers it, drawn first over the whole
// view. Squares keep their size on screen at any zoom; the pattern is anchored to the canvas
// origin, so it travels with a pan (checker.wgsl).

#include "base/palette.hpp"
#include "canvas/item.hpp"

class CheckerItem : public CanvasItem {
public:
    ~CheckerItem() override { releaseGpu(); }

    void setColors(Color8 a, Color8 b);   // `a` is at the origin
    void setCellSize(float points);                       // in view points (scaled for HiDPI)

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override { m_uniform.release(); }

private:
    Color8 m_colorA = Palette::kCanvasCheckerA;
    Color8 m_colorB = Palette::kCanvasCheckerB;
    float m_cell = Palette::kCanvasChecker;

    CheckerParams m_uploaded{};
    bool          m_dirty = true;
    GpuUniform    m_uniform;
};

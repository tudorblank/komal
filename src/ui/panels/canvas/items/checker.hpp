#pragma once
// The canvas background: a checkerboard where nothing covers it, drawn first over the whole
// view. Squares keep their size on screen at any zoom; the pattern is anchored to the canvas
// origin, so it travels with a pan (checker.wgsl). Its colours and squares are the canvas
// style's, read as it draws: a theme change shows at once.

#include "ui/style/ui-style.hpp"
#include "ui/panels/canvas/canvas-item.hpp"

class CheckerItem : public CanvasItem {
public:
    enum class Kind : uint8_t {
        Canvas,    // behind the canvas's picture
        Picture,   // behind a small picture (the navigator)
    };
    explicit CheckerItem(Kind kind = Kind::Canvas) : m_kind(kind) {}
    ~CheckerItem() override { releaseGpu(); }

    void setCellScale(float scale);   // its squares, times the style's (1: as the style says)

    bool prepare(CanvasFrame& frame) override;
    void drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override { m_uniform.release(); }

private:
    Kind  m_kind;
    float m_cellScale = 1.0f;

    CheckerParams m_uploaded{};
    bool          m_dirty = true;
    GpuUniform    m_uniform;
};

#pragma once
// General-purpose canvas items. More specific ones live next to what they show
// (raster-item.h for compositor output, ui/brush-cursor-item.h for the brush).

#include "gfx/canvas-scene.h"

#include <cstdint>

// A solid rectangle in world space (GPU, color.wgsl). Straight-alpha colour.
class RectItem : public CanvasItem {
public:
    RectItem(float x, float y, float w, float h, float r, float g, float b, float a);
    ~RectItem() override { releaseGpu(); }

    void setRect(float x, float y, float w, float h);
    void setColor(float r, float g, float b, float a);

    bool prepare(CanvasFrame& frame) override;
    void draw(WGPURenderPassEncoder pass, CanvasFrame& frame) override;
    void releaseGpu() override;

private:
    float m_x, m_y, m_w, m_h;
    float m_color[4];
    WGPUBuffer    m_buffer    = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    bool          m_upload    = true;   // uniform out of date
};

// A world-space rectangle outlined in screen space (ImGui overlay): stays 1 px crisp at
// any zoom. Inclusive pixel bounds, like BoundsI.
class OutlineItem : public CanvasItem {
public:
    explicit OutlineItem(uint32_t color = 0xFFFF662E, float thickness = 1.0f)   // ImU32 (ABGR)
        : m_color(color), m_thickness(thickness) {}

    void setPixelBounds(int minX, int minY, int maxX, int maxY);
    void setColor(uint32_t color) { m_color = color; }

    void paintOverlay(ImDrawList* dl, const OverlayContext& ctx) override;
    bool drawsOnGpu() const override { return false; }

private:
    int m_minX = 0, m_minY = 0, m_maxX = -1, m_maxY = -1;   // empty
    uint32_t m_color;
    float    m_thickness;
};

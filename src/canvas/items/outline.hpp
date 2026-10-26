#pragma once

#include "canvas/item.hpp"

#include <cstdint>

// Inclusive pixel bounds, like BoundsI.
class OutlineItem : public CanvasItem {
public:
    explicit OutlineItem(uint32_t color = 0xFFF5B48C, float thickness = 1.0f)   // ImU32 (ABGR): the theme's pastel blue
        : m_color(color), m_thickness(thickness) {}

    void setPixelBounds(int minX, int minY, int maxX, int maxY);
    void setColor(uint32_t color) { m_color = color; }

    void drawOverlay(ImDrawList* dl, const OverlayContext& ctx) override;
    bool drawsOnGpu() const override { return false; }

private:
    int m_minX = 0, m_minY = 0, m_maxX = -1, m_maxY = -1;   // empty
    uint32_t m_color;
    float    m_thickness;
};
#pragma once

#include "base/palette.hpp"
#include "canvas/item.hpp"

#include <cstdint>

// Inclusive pixel bounds, like BoundsI.
class OutlineItem : public CanvasItem {
public:
    explicit OutlineItem(Color8 color = Palette::kAccent, float thickness = 1.0f) : m_color(color.packed()), m_thickness(thickness) {}

    void setPixelBounds(int minX, int minY, int maxX, int maxY);

    void drawOverlay(ImDrawList* dl, const OverlayContext& ctx) override;
    bool drawsOnGpu() const override { return false; }

private:
    int m_minX = 0, m_minY = 0, m_maxX = -1, m_maxY = -1;   // empty
    uint32_t m_color;   // packed (ImU32)
    float    m_thickness;
};
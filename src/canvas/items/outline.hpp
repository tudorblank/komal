#pragma once

#include "base/palette.hpp"
#include "canvas/item.hpp"

#include <cstdint>

// Inclusive pixel bounds, like BoundsI.
class OutlineItem : public CanvasItem {
public:
    explicit OutlineItem(Color8 color = Palette::kAccent, float thickness = 1.0f) : m_color(color.packed()), m_thickness(thickness) {}

    void setPixelBounds(const BoundsI& bounds);

    void drawOverlay(ImDrawList* dl, const OverlayContext& ctx) override;
    bool drawsOnGpu() const override { return false; }

private:
    BoundsI  m_bounds;
    uint32_t m_color;   // packed (ImU32)
    float    m_thickness;
};
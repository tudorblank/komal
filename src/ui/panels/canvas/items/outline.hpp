#pragma once

#include "ui/style/ui-style.hpp"
#include "ui/panels/canvas/item.hpp"

#include <cstdint>

// Inclusive pixel bounds, like BoundsI.
class OutlineItem : public CanvasItem {
public:
    explicit OutlineItem(Color8 color = Style::canvas().selection, float thickness = Style::canvas().selectionWidth)
        : m_color(color.packed()), m_thickness(thickness) {}

    void setPixelBounds(const BoundsI& bounds);

    void drawOverlay(const Painter& draw, const OverlayContext& ctx) override;
    bool drawsOnGpu() const override { return false; }

private:
    BoundsI  m_bounds;
    uint32_t m_color;   // packed (ImU32)
    float    m_thickness;
};
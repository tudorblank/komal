#pragma once

#include "ui/style/ui-style.hpp"
#include "ui/panels/canvas/canvas-item.hpp"

#include <cstdint>

// Inclusive pixel bounds, like BoundsI. Its colour and width are the canvas style's for what it
// marks, read as it draws.
class OutlineItem : public CanvasItem {
public:
    enum class Kind : uint8_t {
        Selection,     // what the selected nodes cover
        PreviewFrom,   // where a dragged edit's area was
        PreviewTo,     // ... and where it goes
    };
    explicit OutlineItem(Kind kind = Kind::Selection) : m_kind(kind) {}

    void setPixelBounds(const BoundsI& bounds);

    void drawOverlay(const Painter& draw, const OverlayContext& ctx) override;
    bool drawsOnGpu() const override { return false; }

private:
    BoundsI m_bounds;
    Kind    m_kind;
};
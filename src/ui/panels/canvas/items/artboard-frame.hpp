#pragma once
// An artboard on the canvas: its rect's edge and its name above it. Its colour is in the picture
// (the master draws it); this shows where it is, even while it's transparent or hidden.

#include "document/artboard.hpp"
#include "ui/panels/canvas/canvas-item.hpp"

#include <string>

class ArtboardFrameItem : public CanvasItem {
public:
    void set(const Artboard& a);

    void drawOverlay(const Painter& draw, const OverlayContext& ctx) override;
    bool drawsOnGpu() const override { return false; }

private:
    BoundsI     m_rect;
    std::string m_name;
    bool        m_visible = true;
};

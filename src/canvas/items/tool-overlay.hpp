#pragma once
#include "canvas/item.hpp"

class ToolManager;

// ToolOverlayItem (drawOverlay): what the active tool shows over the canvas (the pixel under the
// mouse when zoomed in, a ring too big for a system cursor), drawn by the tool itself: the
// bridge between the canvas and the tools.
class ToolOverlayItem : public CanvasItem {
public:
    explicit ToolOverlayItem(ToolManager& tools) : m_tools(tools) {}

    void setMouse(float worldX, float worldY) { m_x = worldX; m_y = worldY; }

    void drawOverlay(ImDrawList* dl, const OverlayContext& ctx) override;
    bool drawsOnGpu() const override { return false; }

private:
    ToolManager& m_tools;
    float m_x = 0.0f, m_y = 0.0f;
};

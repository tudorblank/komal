#pragma once
#include "canvas/item.hpp"

class ToolManager;

// What the active tool draws over the canvas (Tool::drawOverlay): the bridge to the tools.
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

#pragma once
#include "gfx/canvas-scene.h"

class ToolManager;

// The active tool's cursor on the canvas (a brush outline, ...), painted by the tool itself.
// Overlay-only: moving it never re-renders the canvas.
class ToolOverlayItem : public CanvasItem {
public:
    explicit ToolOverlayItem(ToolManager& tools) : m_tools(tools) {}

    void setMouse(float worldX, float worldY) { m_x = worldX; m_y = worldY; }

    void paintOverlay(ImDrawList* dl, const OverlayContext& ctx) override;
    bool drawsOnGpu() const override { return false; }

private:
    ToolManager& m_tools;
    float m_x = 0.0f, m_y = 0.0f;
};

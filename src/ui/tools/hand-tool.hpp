#pragma once
#include "ui/tools/tool.hpp"

// Pans the canvas with LMB or the pen's tip (also held Space from any tool), following every
// pointer sample. Its properties zoom and fit.
class HandTool : public Tool {
public:
    const char* name() const override { return "Hand"; }
    ActionId pickAction() const override { return Actions::ToolHand; }
    SDL_Keycode shortcut() const override { return SDLK_H; }
    void drawIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) const override;

    void drawProperties(ToolContext& ctx) override;
    void canvasInput(const CanvasInput& in) override;
    bool busy() const override { return m_dragging; }
    void cancel(ActionRegistry&) override { m_dragging = false; }

private:
    bool m_dragging = false;
    bool m_dragPen  = false;   // the drag ends when this device releases
    Vec2 m_last{};             // the drag's last sample, window points
};

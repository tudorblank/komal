#pragma once
#include "command/pointer-capture.hpp"
#include "ui/tools/tool.hpp"

// Pans the canvas with LMB or the pen's tip (also held Space from any tool), following every
// pointer sample. Its properties zoom and fit.
class HandTool : public Tool {
public:
    const char* name() const override { return "Hand"; }
    ActionId pickAction() const override { return Actions::ToolHand; }
    void drawIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) const override;

    void drawProperties(ToolContext& ctx) override;
    void canvasInput(const CanvasInput& in) override;
    bool busy() const override { return m_drag.held(); }
    void finish(ActionRegistry&) override { m_drag.release(); }

private:
    PointerCapture m_drag;
    Vec2 m_last{};   // the drag's last sample, window points
};

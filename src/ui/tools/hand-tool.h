#pragma once
#include "ui/tools/tool.h"

// Pans the canvas with LMB (also held Space from any tool). Its properties zoom and fit.
class HandTool : public Tool {
public:
    const char* name() const override { return "Hand"; }
    ImGuiKey    shortcut() const override { return ImGuiKey_H; }
    void drawIcon(ImDrawList* dl, ImVec2 center, float size, ImU32 color) const override;

    void drawProperties(ToolContext& ctx) override;
    void canvasInput(const CanvasInput& in) override;
    ImGuiMouseCursor cursor() const override { return m_dragging ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Hand; }
    bool busy() const override { return m_dragging; }
    void cancel(Project&) override { m_dragging = false; }

private:
    bool m_dragging = false;
};

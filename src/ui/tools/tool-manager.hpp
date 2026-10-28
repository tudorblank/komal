#pragma once
#include "core/actions.hpp"
#include "ui/tools/tool.hpp"

#include <memory>
#include <vector>

class BrushTool;

// Owns the tools. active(): the picked tool, or the hand while Space is held over the canvas.
// Their actions (picking one, the brush's settings, colours) are in ui/app-actions.
class ToolManager {
public:
    ToolManager();
    ~ToolManager();

    const std::vector<std::unique_ptr<Tool>>& tools() const { return m_tools; }
    Tool& selected() const { return *m_selected; }
    Tool& active() const { return m_springHand ? *m_hand : *m_selected; }
    void  select(Tool& tool, ActionRegistry& actions);   // the one picked finishes what it was doing
    BrushTool& brush() { return *m_brush; }
    // Space held over the canvas; released only once the hand's drag is done.
    void setSpringHand(bool held);

    ToolColors& colors() { return m_colors; }

private:
    std::vector<std::unique_ptr<Tool>> m_tools;
    Tool*      m_selected = nullptr;
    Tool*      m_hand     = nullptr;
    BrushTool* m_brush    = nullptr;
    bool       m_springHand = false;
    ToolColors m_colors;
};

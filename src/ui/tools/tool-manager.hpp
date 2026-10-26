#pragma once
#include "core/actions.hpp"
#include "ui/tools/tool.hpp"

#include <memory>
#include <vector>

class BrushTool;

// Owns the tools and which one is picked. The canvas asks for active(): the picked tool, or
// the hand while Space is held over the canvas (Photoshop's spring-loaded hand).
class ToolManager {
public:
    ToolManager();
    ~ToolManager();

    const std::vector<std::unique_ptr<Tool>>& tools() const { return m_tools; }
    Tool& selected() const { return *m_selected; }
    Tool& active() const { return m_springHand ? *m_hand : *m_selected; }
    void  select(Tool& tool, Project& project);

    // "tool.<name>" for every tool (its shortcut() as the key), "tool.eraser" (the brush in
    // erase mode), "color.swap" (X: primary <-> secondary), and whatever each tool registers.
    void registerActions(ActionRegistry& actions, Project& project);
    // Space held over the canvas; released only once the hand's drag is done.
    void setSpringHand(bool held);

    ToolColors&       colors() { return m_colors; }
    CanvasNavigation& canvas() { return m_canvas; }

private:
    std::vector<std::unique_ptr<Tool>> m_tools;
    Tool*      m_selected = nullptr;
    Tool*      m_hand     = nullptr;
    BrushTool* m_brush    = nullptr;
    bool       m_springHand = false;
    ToolColors       m_colors;
    CanvasNavigation m_canvas;
};

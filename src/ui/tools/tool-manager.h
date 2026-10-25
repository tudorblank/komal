#pragma once
#include "ui/tools/tool.h"

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

    // B brush, E brush in erase mode, H hand; ignored while typing. Call once per frame.
    void handleShortcuts(Project& project);
    // Space held over the canvas; released only once the hand's drag is done.
    void setSpringHand(bool held);

    // Every mouse position the system reported this frame (screen points, oldest first), so a
    // stroke follows the real path rather than one straight segment per frame.
    void addMouseSample(float x, float y) { m_mouseTrail.push_back(ImVec2(x, y)); }
    const std::vector<ImVec2>& mouseTrail() const { return m_mouseTrail; }
    void clearMouseTrail() { m_mouseTrail.clear(); }

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
    std::vector<ImVec2> m_mouseTrail;
};

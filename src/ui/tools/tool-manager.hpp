#pragma once
#include "command/actions.hpp"
#include "ui/tools/tool.hpp"

#include <memory>
#include <vector>

// Owns the tools and the colours they share. active(): the picked tool, or the hand while Space
// is held over the canvas.
class ToolManager {
public:
    ToolManager();
    ~ToolManager();

    // Picking each tool ("tool.<name>", on its key), the colours, then every tool's own actions.
    void registerActions(ActionRegistry& actions);

    const std::vector<std::unique_ptr<Tool>>& tools() const { return m_tools; }
    Tool& selected() const { return *m_selected; }
    Tool& active() const { return m_springHand ? *m_hand : *m_selected; }
    void  select(Tool& tool, ActionRegistry& actions);   // the one picked finishes what it was doing
    // Space held over the canvas; released only once the hand's drag is done.
    void setSpringHand(bool held);

    const ToolColors& colors() const { return m_colors; }   // changed by color.set / color.swap

private:
    std::vector<std::unique_ptr<Tool>> m_tools;
    Tool*      m_selected = nullptr;
    Tool*      m_hand     = nullptr;
    bool       m_springHand = false;
    ToolColors m_colors;
};

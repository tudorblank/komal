#pragma once

class ActionRegistry;
class Project;
class ToolManager;
struct CanvasNavigation;

// The Toolbox: a fixed bar of tool buttons under the menu bar (not dockable).
class ToolboxPanel {
public:
    void draw(ToolManager& tools, ActionRegistry& actions);   // before the dockspace
    bool& open() { return m_open; }   // shown (View > Toolbar)

private:
    bool m_open = true;
};

// The picked tool's settings; switches with the tool.
class ToolPropertiesPanel {
public:
    void draw(ToolManager& tools, Project& project, ActionRegistry& actions, const CanvasNavigation& canvas);
    bool& open() { return m_open; }   // shown (View > Tool Properties, the window's close button)

private:
    bool m_open = true;
};

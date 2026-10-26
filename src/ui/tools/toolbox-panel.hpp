#pragma once

class ActionRegistry;
class Project;
class ToolManager;

// The Toolbox: a fixed bar of tool buttons under the menu bar (not dockable).
class ToolboxPanel {
public:
    void draw(ToolManager& tools, Project& project, const ActionRegistry& actions);   // before the dockspace
};

// The picked tool's settings; switches with the tool.
class ToolPropertiesPanel {
public:
    void draw(bool* open, ToolManager& tools, Project& project);
};

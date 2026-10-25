#pragma once

class Project;
class ToolManager;

// The Toolbox: a narrow dockable strip of tool buttons (shortcut in the tooltip) with the
// foreground colour swatch underneath, like Photoshop's.
class ToolboxPanel {
public:
    void draw(bool* open, ToolManager& tools, Project& project);
};

// The picked tool's settings; switches with the tool.
class ToolPropertiesPanel {
public:
    void draw(bool* open, ToolManager& tools, Project& project);
};

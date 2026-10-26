#pragma once

class ActionRegistry;
class Project;
class ToolManager;

// The Toolbox: a fixed toolbar right under the menu bar, tool buttons in a row (shortcut in the
// tooltip). Not a dockable window: it is part of the main window's frame, like the menu bar,
// and can't be moved, docked or resized. (The colour is picked in the Color panel.)
class ToolboxPanel {
public:
    void draw(ToolManager& tools, Project& project, const ActionRegistry& actions);   // before the dockspace
};

// The picked tool's settings; switches with the tool.
class ToolPropertiesPanel {
public:
    void draw(bool* open, ToolManager& tools, Project& project);
};

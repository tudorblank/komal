#pragma once
#include "ui/panel.hpp"

// The Toolbox: a fixed bar of tool buttons under the menu bar (not dockable).
class ToolboxPanel : public Panel {
public:
    PanelInfo info() const override;
    bool init() override;
    void draw() override;   // before the dockspace (DockSlot::Toolbar)

private:
};

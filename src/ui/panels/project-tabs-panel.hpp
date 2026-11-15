#pragma once
#include "ui/panels/panel-base.hpp"

// The open projects: a fixed strip of tabs under the Toolbox, one band with it (not dockable).
// Picking a tab is project.activate; its cross, or a middle click, is file.close (asked first if it's
// unsaved); a double click on the strip past the tabs is project.new.
class ProjectTabsPanel : public Panel {
public:
    PanelInfo info() const override;
    void draw() override;   // before the dockspace, after the Toolbox (DockSlot::Toolbar)

private:
    void drawTabs(ProjectId& pick, ProjectId& close);   // what was clicked: run once the bar is done
};

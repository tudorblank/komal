#pragma once
#include "ui/panels/panel-base.hpp"

// The open documents: a fixed strip of tabs under the toolbar, one band with it (not dockable).
// Picking a tab is doc.activate; its cross, or a middle click, is file.close (asked first if it's
// unsaved); a double click on the strip past the tabs is doc.new.
class DocumentTabsPanel : public Panel {
public:
    PanelInfo info() const override;
    void draw() override;   // before the dockspace, after the toolbar (DockSlot::Toolbar)
};

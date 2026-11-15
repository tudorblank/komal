#pragma once
#include "ui/panels/panel-base.hpp"

// The picked tool's settings; switches with the tool.
class ToolPropertiesPanel : public Panel {
public:
    PanelInfo info() const override;
    void draw() override;
};

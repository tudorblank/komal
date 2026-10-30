#pragma once
#include "ui/panel.hpp"

// The picked tool's settings; switches with the tool.
class ToolPropertiesPanel : public Panel {
public:
    PanelInfo info() const override;
    bool init() override;
    void draw() override;

private:
};

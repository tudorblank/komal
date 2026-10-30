#pragma once
// ImGui's own demo window (View > ImGui demo): every widget ImGui has, as a reference.

#include "ui/panel.hpp"

class DemoPanel : public Panel {
public:
    PanelInfo info() const override;
    bool init() override;
    void draw() override;

private:
};

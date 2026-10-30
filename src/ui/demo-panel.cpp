#include "ui/demo-panel.hpp"
#include "command/actions.hpp"

#include <imgui.h>

PanelInfo DemoPanel::info() const { return { Actions::ViewImguiDemo, "ImGui demo", DockSlot::Floating, DrawPhase::Edit, false }; }

bool DemoPanel::init()
{
    return true;
}

void DemoPanel::draw()
{
    if (!isOpen()) return;
    bool keep = true;
    ImGui::ShowDemoWindow(&keep);
    if (!keep) ui().actions.run(toggleAction());   // its close button
}

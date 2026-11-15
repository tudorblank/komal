#include "ui/panels/tool-properties-panel.hpp"
#include "ui/panels/panel-window.hpp"
#include "ui/tools/tool-manager.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>

PanelInfo ToolPropertiesPanel::info() const { return { Actions::ViewToolProperties, "Tool Properties", DockSlot::RightLower }; }

void ToolPropertiesPanel::draw()
{
    if (!isOpen()) return;
    ToolManager& tools = ui().tools;
    const Widgets::PanelWindow window(*this, ui().actions);
    if (!window) return;
    Tool& tool = tools.selected();
    Widgets::heading(tool.name());
    ToolContext ctx{ ui().workspace(), ui().actions };
    ImGui::PushID(&tool);
    tool.drawProperties(ctx);
    ImGui::PopID();
}

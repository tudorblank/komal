#include "ui/tools/tool-properties-panel.hpp"
#include "ui/tools/tool-manager.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>

PanelInfo ToolPropertiesPanel::info() const { return { Actions::ViewToolProperties, "Tool Properties", DockSlot::RightLower }; }

bool ToolPropertiesPanel::init()
{
    return true;
}

void ToolPropertiesPanel::draw()
{
    if (!isOpen()) return;
    ToolManager& tools = ui().tools;
    const Widgets::PanelWindow window(*this, ui().actions);
    if (!window) return;
    Tool& tool = tools.selected();
    ImGui::SeparatorText(tool.name());
    ToolContext ctx{ ui().project, ui().actions, tools };
    ImGui::PushID(&tool);
    tool.drawProperties(ctx);
    ImGui::PopID();
}

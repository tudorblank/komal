#include "ui/panels/toolbox-panel.hpp"
#include "ui/panels/panel-window.hpp"
#include "command/actions.hpp"
#include "ui/tools/tool-manager.hpp"
#include "ui/render/skins.hpp"
#include "ui/style/ui-style.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>

PanelInfo ToolboxPanel::info() const { return { Actions::ViewToolbox, "Toolbox", DockSlot::Toolbar }; }

void ToolboxPanel::draw()
{
    if (!isOpen()) return;
    ToolManager& tools = ui().tools;
    ActionRegistry& actions = ui().actions;
    const Style::ToolButtonStyle& style = Style::get().toolButton;
    const float size = style.size, pad = style.padding;
    // One band with the menu bar.
    const Widgets::BarWindow bar(*this, size + 2.0f * pad, ImVec2(pad * 2.0f, pad), style.bar);
    if (bar) {
        const Painter draw = Painter::window();
        for (const auto& t : tools.tools()) {
            ImGui::PushID(t.get());
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##tool", ImVec2(size, size))) actions.run(t->pickAction());
            const bool selected = &tools.selected() == t.get();
            Skin::toolButton(draw, style, p, ImVec2(p.x + size, p.y + size), selected, ImGui::IsItemHovered());
            t->drawIcon(draw, ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * style.iconSize, selected ? style.icon : style.iconDim);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))   // the label is looked up only when shown
                ImGui::SetTooltip("%s (%s)", t->name(), actions.shortcutLabel(t->pickAction()).c_str());   // as bound
            ImGui::PopID();
            ImGui::SameLine(0.0f, style.gap);
        }
    }
}

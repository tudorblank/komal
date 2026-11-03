#include "toolbox-panel.hpp"
#include "command/actions.hpp"
#include "ui/tools/tool-manager.hpp"
#include "ui/render/skins.hpp"
#include "ui/style/ui-style.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // BeginViewportSideBar

PanelInfo ToolboxPanel::info() const { return { Actions::ViewTools, "Toolbar", DockSlot::Toolbar }; }

bool ToolboxPanel::init()
{
    return true;
}

void ToolboxPanel::draw()
{
    if (!isOpen()) return;
    ToolManager& tools = ui().tools;
    ActionRegistry& actions = ui().actions;
    const Style::ToolButtonStyle& style = Style::get().toolButton;
    // A viewport side bar under the menu bar: the dockspace lays out below it.
    const float size = style.size, pad = style.padding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad * 2.0f, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pad, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, style.bar);   // one header with the menu bar
    const bool shown = ImGui::BeginViewportSideBar("##toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, size + 2.0f * pad,
                                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                   ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (shown) {
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
            ImGui::SameLine();
        }
    }
    ImGui::End();   // a side bar is a window: always ended
}

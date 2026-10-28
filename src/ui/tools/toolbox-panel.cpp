#include "toolbox-panel.hpp"
#include "core/actions.hpp"
#include "core/project.hpp"
#include "ui/panel-titles.hpp"
#include "ui/tools/tool-manager.hpp"
#include "ui/widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // BeginViewportSideBar
#include <cmath>

void ToolboxPanel::draw(ToolManager& tools, ActionRegistry& actions)
{
    if (!m_open) return;
    // A viewport side bar under the menu bar: the dockspace lays out below it.
    const float size = std::round(ImGui::GetFontSize() * 1.75f);   // a tool button
    const float pad  = std::round(ImGui::GetFontSize() * 0.25f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad * 2.0f, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pad, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));   // one header with the menu bar
    const bool shown = ImGui::BeginViewportSideBar("##toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, size + 2.0f * pad,
                                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                   ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (shown) {
        const ImGuiStyle& st = ImGui::GetStyle();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (const auto& t : tools.tools()) {
            ImGui::PushID(t.get());
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##tool", ImVec2(size, size))) actions.run(toolActionId(*t));
            const bool selected = &tools.selected() == t.get();
            const bool hovered  = ImGui::IsItemHovered();
            const ImVec2 q(p.x + size, p.y + size);
            if (selected || hovered)
                dl->AddRectFilled(p, q, ImGui::GetColorU32(selected ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), st.FrameRounding);
            if (selected) dl->AddRect(p, q, ImGui::GetColorU32(ImGuiCol_CheckMark), st.FrameRounding);
            t->drawIcon(dl, ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.62f,
                        ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))   // the label is looked up only when shown
                ImGui::SetTooltip("%s (%s)", t->name(), actions.shortcutLabel(toolActionId(*t)).c_str());   // as bound
            ImGui::PopID();
            ImGui::SameLine();
        }
    }
    ImGui::End();   // a side bar is a window: always ended
}

void ToolPropertiesPanel::draw(ToolManager& tools, Project& project, ActionRegistry& actions, const CanvasNavigation& canvas)
{
    if (!m_open) return;
    if (!Widgets::beginPanel(PanelTitle::kToolProperties, actions, "view.toolProperties")) { Widgets::endPanel(); return; }
    Tool& tool = tools.selected();
    ImGui::SeparatorText(tool.name());
    ToolContext ctx{ project, actions, tools, canvas };
    ImGui::PushID(&tool);
    tool.drawProperties(ctx);
    ImGui::PopID();
    Widgets::endPanel();
}

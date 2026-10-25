#include "toolbox-panel.h"
#include "core/project.h"
#include "ui/tools/tool-manager.h"

#include <imgui.h>
#include <algorithm>

namespace {

// The docked toolbox shows no tab bar of its own (it can still be undocked by dragging the
// bar that appears on hover).
void noTabBar()
{
    ImGuiWindowClass wc;
    wc.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_AutoHideTabBar;
    ImGui::SetNextWindowClass(&wc);
}

const char* keyName(ImGuiKey key) { return ImGui::GetKeyName(key); }

} // namespace

void ToolboxPanel::draw(bool* open, ToolManager& tools, Project& project)
{
    noTabBar();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 6.0f));
    const bool shown = ImGui::Begin("Tools", open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!shown) { ImGui::End(); return; }

    const ImGuiStyle& st = ImGui::GetStyle();
    const float size = ImGui::GetFontSize() * 2.1f;
    const float x = std::max(0.0f, (ImGui::GetContentRegionAvail().x - size) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (const auto& t : tools.tools()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
        ImGui::PushID(t.get());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##tool", ImVec2(size, size))) tools.select(*t, project);
        const bool selected = &tools.selected() == t.get();
        const bool hovered  = ImGui::IsItemHovered();
        const ImVec2 q(p.x + size, p.y + size);
        if (selected || hovered)
            dl->AddRectFilled(p, q, ImGui::GetColorU32(selected ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), st.FrameRounding);
        if (selected) dl->AddRect(p, q, ImGui::GetColorU32(ImGuiCol_CheckMark), st.FrameRounding);
        t->drawIcon(dl, ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.62f,
                    ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_TextDisabled));
        ImGui::SetItemTooltip("%s (%s)", t->name(), keyName(t->shortcut()));
        ImGui::PopID();
    }

    // Foreground colour, under the tools.
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
    float* fg = tools.colors().foreground;
    if (ImGui::ColorButton("##fg", ImVec4(fg[0], fg[1], fg[2], fg[3]), ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(size, size)))
        ImGui::OpenPopup("##fg-picker");
    ImGui::SetItemTooltip("Foreground colour");
    if (ImGui::BeginPopup("##fg-picker")) {
        ImGui::ColorPicker4("##picker", fg, ImGuiColorEditFlags_AlphaBar);
        ImGui::EndPopup();
    }
    ImGui::End();
}

void ToolPropertiesPanel::draw(bool* open, ToolManager& tools, Project& project)
{
    if (!ImGui::Begin("Tool Properties", open)) { ImGui::End(); return; }
    Tool& tool = tools.selected();
    ImGui::SeparatorText(tool.name());
    ToolContext ctx{ project, tools, tools.canvas() };
    ImGui::PushID(&tool);
    tool.drawProperties(ctx);
    ImGui::PopID();
    ImGui::End();
}

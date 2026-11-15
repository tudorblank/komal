#include "ui/panels/project-tabs-panel.hpp"
#include "ui/panels/panel-window.hpp"
#include "command/actions.hpp"
#include "project/projects-manager-actions.hpp"
#include "ui/render/skins.hpp"
#include "ui/style/ui-style.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // ImGuiButtonFlags_PressedOnClick
#include <algorithm>
#include <vector>

PanelInfo ProjectTabsPanel::info() const { return { Actions::ViewProjects, "Projects", DockSlot::Toolbar }; }

void ProjectTabsPanel::draw()
{
    if (!isOpen()) return;
    const Style::ProjectTabStyle& style = Style::get().projectTab;
    ProjectId pick = kNoProject, close = kNoProject;   // run after the tabs, never among them: the projects (and the tabs) may change
    if (const Widgets::BarWindow bar(*this, style.height, ImVec2(style.margin, (style.height - style.tabHeight) * 0.5f), style.bar); bar)
        drawTabs(pick, close);
    if (pick != kNoProject && pick != ui().projects.activeId()) ui().actions.run(Actions::ProjectActivate, { { ProjectArgs::kProject, pick } });
    if (close != kNoProject) ui().actions.run(Actions::FileClose, { { ProjectArgs::kProject, close } });
}

void ProjectTabsPanel::drawTabs(ProjectId& pick, ProjectId& close)
{
    const Style::ProjectTabStyle& style = Style::get().projectTab;

    // Each tab as wide as its name and mark; all of them narrower, alike, when they don't fit.
    const ProjectsManager& projects = ui().projects;
    const ProjectId active = projects.activeId();
    const float avail = ImGui::GetContentRegionAvail().x;
    std::vector<float> widths;
    float total = 0.0f;
    for (const auto& d : projects.all()) {
        const float w = std::clamp(Skin::projectTabWidth(style, ImGui::CalcTextSize(d->name().c_str()).x), style.minWidth, style.maxWidth);
        widths.push_back(w);
        total += w + style.gap;
    }
    const float fit = total > avail ? std::max(style.minWidth, avail / float(widths.size()) - style.gap) : style.maxWidth;

    const Painter draw = Painter::window();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (size_t i = 0; i < projects.all().size(); ++i) {
        const Project& d = *projects.all()[i];
        ImGui::PushID(int(d.id()));
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const ImVec2 b(a.x + std::min(widths[i], fit), a.y + style.tabHeight);
        ImGui::InvisibleButton("##tab", ImVec2(b.x - a.x, b.y - a.y),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_PressedOnClick);
        const bool hot = ImGui::IsItemHovered();
        const Skin::Box markBox = Skin::projectTabMark(style, a, b);
        const bool onMark = hot && mouse.x >= markBox.min.x && mouse.x < markBox.max.x;
        if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) close = d.id();
        else if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) (onMark ? close : pick) = d.id();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !d.path().empty()) ImGui::SetTooltip("%s", d.path().c_str());
        const Skin::TabMark mark = onMark ? Skin::TabMark::CloseHot : d.unsaved() ? Skin::TabMark::Unsaved : Skin::TabMark::Close;
        Skin::projectTab(draw, style, a, b, d.id() == active, hot, d.name().c_str(), mark);
        ImGui::PopID();
        ImGui::SameLine(0.0f, style.gap);   // the gap the fit above counts
    }
    // The strip past the tabs: a double click there is a new project (like an editor's).
    const float rest = ImGui::GetContentRegionAvail().x;
    if (rest >= 1.0f) {
        ImGui::InvisibleButton("##empty", ImVec2(rest, style.tabHeight));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ui().actions.run(Actions::ProjectNew);
    }
}

#include "document-tabs-panel.hpp"
#include "command/actions.hpp"
#include "document/documents-actions.hpp"
#include "ui/render/skins.hpp"
#include "ui/style/ui-style.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // BeginViewportSideBar
#include <algorithm>
#include <vector>

PanelInfo DocumentTabsPanel::info() const { return { Actions::ViewDocuments, "Documents", DockSlot::Toolbar }; }

void DocumentTabsPanel::draw()
{
    if (!isOpen()) return;
    const Style::DocumentTabStyle& style = Style::get().documentTab;
    const float pad = (style.height - style.tabHeight) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.margin, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.gap, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, style.bar);
    const bool shown = ImGui::BeginViewportSideBar("##documents", ImGui::GetMainViewport(), ImGuiDir_Up, style.height,
                                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                                   ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (!shown) {
        ImGui::End();   // a side bar is a window: always ended
        return;
    }

    // Each tab as wide as its name and mark; all of them narrower, alike, when they don't fit.
    const Documents& docs = ui().documents;
    const DocumentId active = docs.activeId();
    const float avail = ImGui::GetContentRegionAvail().x;
    std::vector<float> widths;
    float total = 0.0f;
    for (const auto& d : docs.all()) {
        const float w = std::clamp(style.padding * 1.5f + ImGui::CalcTextSize(d->name().c_str()).x + style.mark, style.minWidth, style.maxWidth);
        widths.push_back(w);
        total += w + style.gap;
    }
    const float fit = total > avail ? std::max(style.minWidth, avail / float(widths.size()) - style.gap) : style.maxWidth;

    // Run after the tabs, never among them: the documents (and the tabs) may change.
    DocumentId pick = kNoDocument, close = kNoDocument;
    const Painter draw = Painter::window();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (size_t i = 0; i < docs.all().size(); ++i) {
        const Document& d = *docs.all()[i];
        ImGui::PushID(int(d.id()));
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const ImVec2 b(a.x + std::min(widths[i], fit), a.y + style.tabHeight);
        ImGui::InvisibleButton("##tab", ImVec2(b.x - a.x, b.y - a.y),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_PressedOnClick);
        const bool hot = ImGui::IsItemHovered();
        const float markX = b.x - style.padding * 0.5f - style.mark;
        const bool onMark = hot && mouse.x >= markX && mouse.x < markX + style.mark;
        if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) close = d.id();
        else if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) (onMark ? close : pick) = d.id();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !d.path().empty()) ImGui::SetTooltip("%s", d.path().c_str());
        const Skin::TabMark mark = onMark ? Skin::TabMark::CloseHot : d.unsaved() ? Skin::TabMark::Unsaved : Skin::TabMark::Close;
        Skin::documentTab(draw, style, a, b, d.id() == active, hot, d.name().c_str(), mark);
        ImGui::PopID();
        ImGui::SameLine();
    }
    // The strip past the tabs: a double click there is a new document (like an editor's).
    const float rest = ImGui::GetContentRegionAvail().x;
    if (rest >= 1.0f) {
        ImGui::InvisibleButton("##empty", ImVec2(rest, style.tabHeight));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ui().actions.run(Actions::DocNew);
    }
    ImGui::End();

    if (pick != kNoDocument && pick != active) ui().actions.run(Actions::DocActivate, { { DocArgs::kDocument, pick } });
    if (close != kNoDocument) ui().actions.run(Actions::FileClose, { { DocArgs::kDocument, close } });
}

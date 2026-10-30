#include "ui/graph-panel/node-menu.hpp"

#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <cctype>
#include <cstring>
#include <vector>

namespace {

constexpr const char* kPopup = "##add-node";

bool matches(const char* name, const char* search)
{
    const size_t n = std::strlen(name), m = std::strlen(search);
    for (size_t i = 0; i + m <= n; ++i) {
        size_t j = 0;
        while (j < m && std::tolower((unsigned char)name[i + j]) == std::tolower((unsigned char)search[j])) ++j;
        if (j == m) return true;
    }
    return m == 0;
}

}

void NodeMenu::open(Fit fit)
{
    m_fit        = fit;
    m_search[0]  = 0;
    m_highlight  = 0;
    m_open       = true;
    m_justOpened = true;   // the popup opens in draw(), inside the graph's window (its ID stack)
}

bool NodeMenu::draw(NodeType& picked)
{
    if (!m_open) return false;
    if (m_justOpened) ImGui::OpenPopup(kPopup);
    if (!ImGui::BeginPopup(kPopup)) { m_open = false; return false; }

    // Search field first, focused when the menu opens.
    if (m_justOpened) { ImGui::SetKeyboardFocusHere(); m_justOpened = false; }
    Widgets::searchField("##search", "Search...", m_search, sizeof m_search, ImGui::GetFontSize() * 12.0f);

    std::vector<const NodeKind*> items;
    for (const NodeKind& k : nodeKinds()) {
        if (m_fit == Fit::NeedsInput && k.inputs == InputShape::None) continue;   // every kind has an output
        if (matches(k.label, m_search)) items.push_back(&k);
    }
    // ImGui's keys: the search field owns the text focus.
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++m_highlight;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   --m_highlight;
    m_highlight = items.empty() ? 0 : (m_highlight + int(items.size())) % int(items.size());

    ImGui::Separator();
    bool done = false;
    if (items.empty()) ImGui::TextDisabled("No match");
    for (size_t i = 0; i < items.size(); ++i) {
        if (ImGui::Selectable(items[i]->label, int(i) == m_highlight)) { picked = items[i]->type; done = true; }
        ImGui::SameLine(ImGui::GetFontSize() * 7.0f);
        ImGui::TextDisabled("%s", items[i]->hint);
    }
    if (!done && !items.empty() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
        picked = items[size_t(m_highlight)]->type;
        done = true;
    }
    if (done || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        m_open = false;
    }
    ImGui::EndPopup();
    return done;
}

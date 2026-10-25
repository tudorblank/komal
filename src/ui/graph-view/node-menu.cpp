#include "node-menu.h"

#include <imgui.h>
#include <cctype>
#include <cstring>
#include <vector>

namespace {

constexpr const char* kPopup = "##add-node";

struct NodeTypeInfo {
    NodeType    type;
    const char* name;
    const char* hint;
    bool        hasInput;
};

constexpr NodeTypeInfo kTypes[] = {
    { NodeType::Raster,     "Raster",     "pixels you paint",          false },
    { NodeType::Reference,  "Reference",  "another view of its input", true  },
    { NodeType::Move,       "Move",       "shifts its input",          true  },
    { NodeType::Compositor, "Compositor", "stacks layers",             true  },
};

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

} // namespace

void NodeMenu::open(Fit fit)
{
    m_fit        = fit;
    m_search[0]  = 0;
    m_highlight  = 0;
    m_open       = true;
    m_justOpened = true;
    ImGui::OpenPopup(kPopup);
}

bool NodeMenu::draw(NodeType& picked)
{
    if (!m_open) return false;
    if (!ImGui::BeginPopup(kPopup)) { m_open = false; return false; }

    // Search field first, focused when the menu opens.
    if (m_justOpened) { ImGui::SetKeyboardFocusHere(); m_justOpened = false; }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::InputTextWithHint("##search", "Search...", m_search, sizeof m_search);

    std::vector<const NodeTypeInfo*> items;
    for (const NodeTypeInfo& t : kTypes) {
        if (m_fit == Fit::NeedsInput && !t.hasInput) continue;   // every type has an output
        if (matches(t.name, m_search)) items.push_back(&t);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++m_highlight;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   --m_highlight;
    m_highlight = items.empty() ? 0 : (m_highlight + int(items.size())) % int(items.size());

    ImGui::Separator();
    bool done = false;
    if (items.empty()) ImGui::TextDisabled("No match");
    for (size_t i = 0; i < items.size(); ++i) {
        if (ImGui::Selectable(items[i]->name, int(i) == m_highlight)) { picked = items[i]->type; done = true; }
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

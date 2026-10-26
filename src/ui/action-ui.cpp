#include "action-ui.hpp"

#include <imgui.h>

void actionMenuItem(ActionRegistry& actions, std::string_view id)
{
    const Action* a = actions.find(id);
    if (!a) return;
    const std::string shortcut = actions.shortcutLabel(id);
    const bool checked = a->checked && a->checked();
    if (ImGui::MenuItem(a->title.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), checked, a->isEnabled()))
        actions.run(id);
}

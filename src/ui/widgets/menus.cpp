#include "ui/widgets/detail.hpp"
#include "command/actions.hpp"

using namespace Widgets::detail;

namespace Widgets {

// ---------------------------------------------------------------- menus

ActionResult actionMenuItem(ActionRegistry& actions, ActionId id, const ActionArgs& args, const MenuEntry& entry)
{
    const Action* a = actions.find(id);
    if (!a) return {};
    const std::string shortcut = actions.shortcutLabel(id);
    const bool checked = entry.checked ? *entry.checked : a->checked && a->checked();
    if (!ImGui::MenuItem(entry.label ? entry.label : a->title.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), checked,
                         entry.enabled && a->isEnabled()))
        return {};
    return actions.run(id, args);
}

void hint(ActionRegistry& actions, std::initializer_list<HelpPart> parts)
{
    ImGui::TextDisabled("%s", actions.describe(parts).c_str());
}

}

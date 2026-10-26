#pragma once
// User actions (Undo, Import, Brush tool, ...), each registered once by its owner with a stable
// id, title, shortcuts, scope and enabled state. Menus, shortcuts and tooltips all read this one
// list. (ProjectEvents say what changed; actions are what the user asks for.)
//
//   actions.add({ .id = "edit.undo", .title = "Undo", .shortcuts = { { SDLK_Z, ModPrimary } },
//                 .repeats = true,
//                 .enabled = [&] { return project.canUndo(); },
//                 .run     = [&] { project.undo(); } });
//
// Shortcuts fire through dispatch(), called by each scope's owner while it's live: the canvas
// while it has the pointer, the graph while hovered or focused (unless the canvas has the
// pointer), the global scope unless a text field is typing. Never two panels at once.

#include "core/input.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

enum class ActionScope : uint8_t {
    Global,
    Canvas,
    Graph,
};

struct Action {
    std::string id{};                    // "group.name": stable, what menus and tooltips refer to
    std::string title{};                 // as menus show it
    std::vector<Shortcut> shortcuts{};   // the first is the one shown; none: menus only
    ActionScope scope = ActionScope::Global;
    bool repeats = false;                // fires again while its keys are held (the OS's key repeat)
    std::function<bool()> enabled{};     // null: always
    std::function<bool()> checked{};     // a toggle, shown with a check mark (null: not one)
    std::function<void()> run{};

    bool isEnabled() const { return !enabled || enabled(); }
};

class ActionRegistry {
public:
    void add(Action action);   // ids are unique; a shortcut means one thing wherever it can fire
    const Action* find(std::string_view id) const;
    bool run(std::string_view id);   // false if unknown or disabled

    // Runs the enabled actions of `scope` whose shortcut went down this frame.
    void dispatch(const InputFrame& input, ActionScope scope);

    std::string shortcutLabel(std::string_view id) const;   // "Ctrl+Shift+Z" ("" if none)
    const std::vector<Action>& actions() const { return m_actions; }

private:
    std::vector<Action> m_actions;
};

std::string shortcutLabel(const Shortcut& s);   // Cmd on macOS

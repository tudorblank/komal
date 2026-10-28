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
// Shortcuts fire in one place, dispatch(), once a frame before the panels draw: the global ones,
// and those of the one panel scope that has the keyboard. A panel claims it while drawing, for
// the next frame: with the pointer over it, or focused; the pointer wins. Nothing fires while a
// text field is typing.

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

enum class KeyboardClaim : uint8_t {
    Focus,     // the panel's window is focused
    Pointer,   // the pointer is over the panel (or held on it)
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

    void claimKeyboard(ActionScope scope, KeyboardClaim claim);   // the first of the strongest wins
    // Runs the enabled actions whose shortcut went down this frame: global ones, and those of the
    // scope claimed last frame. Once a frame, before the panels draw.
    void dispatch(const InputFrame& input, bool typing);

    std::string shortcutLabel(std::string_view id) const;   // "Ctrl+Shift+Z" ("" if none)
    const std::vector<Action>& actions() const { return m_actions; }

private:
    std::vector<Action> m_actions;
    ActionScope   m_claimScope = ActionScope::Global;   // for the next dispatch
    KeyboardClaim m_claim      = KeyboardClaim::Focus;
    bool          m_claimed    = false;
};

std::string shortcutLabel(const Shortcut& s);   // Cmd on macOS

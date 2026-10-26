#include "actions.hpp"

#include <SDL3/SDL_keyboard.h>
#include <cassert>

namespace {

[[maybe_unused]] bool sameShortcut(const Shortcut& a, const Shortcut& b) { return a.key == b.key && a.mods == b.mods; }

} // namespace

void ActionRegistry::add(Action action)
{
    assert(!find(action.id) && "action ids are unique");
#ifndef NDEBUG
    // Panel scopes are never live together (one has focus, the other the pointer), but the global
    // scope is live with either: a shortcut must not mean two things at once.
    for (const Action& other : m_actions)
        if (other.scope == action.scope || other.scope == ActionScope::Global || action.scope == ActionScope::Global)
            for (const Shortcut& s : action.shortcuts)
                for (const Shortcut& o : other.shortcuts)
                    assert(!sameShortcut(s, o) && "shortcut already taken");
#endif
    m_actions.push_back(std::move(action));
}

const Action* ActionRegistry::find(std::string_view id) const
{
    for (const Action& a : m_actions)
        if (a.id == id) return &a;
    return nullptr;
}

bool ActionRegistry::run(std::string_view id)
{
    const Action* a = find(id);
    if (!a || !a->run || !a->isEnabled()) return false;
    a->run();
    return true;
}

void ActionRegistry::dispatch(const InputFrame& input, ActionScope scope)
{
    if (input.keys.empty()) return;
    for (const Action& a : m_actions) {
        if (a.scope != scope || !a.run) continue;
        for (const Shortcut& s : a.shortcuts)
            if (input.pressed(s, a.repeats)) {
                if (a.isEnabled()) a.run();
                break;
            }
    }
}

std::string ActionRegistry::shortcutLabel(std::string_view id) const
{
    const Action* a = find(id);
    return a && !a->shortcuts.empty() ? ::shortcutLabel(a->shortcuts.front()) : std::string();
}

std::string shortcutLabel(const Shortcut& s)
{
    std::string out;
    if (s.mods & ModCtrl)  out += "Ctrl+";
    if (s.mods & ModAlt)   out += "Alt+";
    if (s.mods & ModShift) out += "Shift+";
#if defined(__APPLE__)
    if (s.mods & ModSuper) out += "Cmd+";
#else
    if (s.mods & ModSuper) out += "Super+";
#endif
    out += SDL_GetKeyName(s.key);
    return out;
}

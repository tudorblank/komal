#include "actions.hpp"
#include "base/clock.hpp"
#include "base/log.hpp"

#include <SDL3/SDL_keyboard.h>
#include <cassert>

namespace {

bool sameShortcut(const Shortcut& a, const Shortcut& b) { return a.key == b.key && a.mods == b.mods; }

bool logged(ActionKind kind) { return kind == ActionKind::DocumentEdit || kind == ActionKind::Gesture || kind == ActionKind::AppState; }

}

const char* actionKindName(ActionKind kind)
{
    switch (kind) {
    case ActionKind::DocumentEdit: return "edit";
    case ActionKind::Gesture:      return "gesture";
    case ActionKind::AppState:     return "app";
    case ActionKind::Interface:    return "interface";
    case ActionKind::Unset:        break;
    }
    return "?";
}

// ---------------------------------------------------------------- registry

ActionRegistry::ActionRegistry() = default;

std::string ActionRegistry::whyNot(const Action& action) const
{
    if (action.id.name().empty() || find(action.id)) return "its id is empty or taken";
    if (action.kind == ActionKind::Unset) return "it doesn't say its kind";
    const bool gesture = action.kind == ActionKind::Gesture;
    if (gesture ? (!action.begin || action.run) : (!action.run || action.begin || action.update || action.end || action.cancel))
        return "a gesture begins (and never runs), anything else runs";
    if (!gesture && (!action.endArgs.empty() || !action.updateArgs.empty())) return "only a gesture ends and updates";
    // Panel scopes are never live together, but the global scope is live with either.
    for (const Action& other : m_actions)
        if (other.scope == action.scope || other.scope == ActionScope::Global || action.scope == ActionScope::Global)
            for (const Shortcut& s : action.shortcuts)
                for (const Shortcut& o : other.shortcuts)
                    if (sameShortcut(s, o)) return "its shortcut is " + std::string(other.id.name()) + "'s";
    return {};
}

void ActionRegistry::add(Action action)
{
    if (const std::string why = whyNot(action); !why.empty()) {
        Log::error("action", "%.*s not added: %s", int(action.id.name().size()), action.id.name().data(), why.c_str());
        assert(false && "a malformed action");
        return;
    }
    m_index.emplace(action.id.name(), m_actions.size());
    m_actions.push_back(std::move(action));
}

const Action* ActionRegistry::find(ActionId id) const
{
    const auto it = m_index.find(id.name());
    return it == m_index.end() ? nullptr : &m_actions[it->second];
}

ActionResult ActionRegistry::run(ActionId id, ActionArgs args)
{
    const Action* a = find(id);
    assert(a && "unknown action");
    if (!a || !a->run) return {};
    if (!a->isEnabled()) {
        record(*a, args, ActionLogEntry::Status::Disabled);
        return {};
    }
    if (!argsFit(*a, a->args, args)) return {};
    openStep(*a);
    const ActionResult r = a->run(args);
    closeStep(*a);
    if (r.made) args.add({ "made", r.made });
    record(*a, args, r ? ActionLogEntry::Status::Done : ActionLogEntry::Status::Refused);
    return r;
}

bool ActionRegistry::beginGesture(ActionId id, ActionArgs args)
{
    const Action* a = find(id);
    assert(a && a->begin && "unknown gesture");
    if (!a || !a->begin) return false;
    if (inGesture(id)) cancelGesture(id);   // never ended: the new one replaces it
    if (!a->isEnabled()) {
        record(*a, args, ActionLogEntry::Status::Disabled);
        return false;
    }
    if (!argsFit(*a, a->args, args)) return false;
    openStep(*a);
    if (!a->begin(args)) {
        closeStep(*a);
        record(*a, args, ActionLogEntry::Status::Refused);
        return false;
    }
    args.payload.reset();   // only for the handler
    m_gestures[id.name()] = std::move(args);
    return true;
}

void ActionRegistry::updateGesture(ActionId id, const ActionArgs& args)
{
    if (!inGesture(id)) return;
    const Action* a = find(id);
    if (!a || !a->update || !argsFit(*a, a->updateArgs, args)) return;   // a mistaken update is refused (logged), the gesture goes on
    a->update(args);
}

ActionResult ActionRegistry::endGesture(ActionId id, ActionArgs args)
{
    auto it = m_gestures.find(id.name());
    if (it == m_gestures.end()) return {};
    ActionArgs logged = std::move(it->second);
    m_gestures.erase(it);
    const Action* a = find(id);
    if (a && !argsFit(*a, a->endArgs, args)) {   // ends all the same: as if cancelled
        if (a->cancel) a->cancel(ActionArgs{});
        closeStep(*a);
        return {};
    }
    // The end sees the whole gesture's arguments: begin's, with the end's on top.
    ActionArgs all = logged;
    all.append(args);
    all.payload = args.payload;
    const ActionResult r = a && a->end ? a->end(all) : ActionResult{ true };
    if (a) closeStep(*a);
    logged.append(args);
    if (r.made) logged.add({ "made", r.made });
    if (a) record(*a, logged, r ? ActionLogEntry::Status::Done : ActionLogEntry::Status::Refused);
    return r;
}

void ActionRegistry::cancelGesture(ActionId id)
{
    auto it = m_gestures.find(id.name());
    if (it == m_gestures.end()) return;
    const ActionArgs logged = std::move(it->second);
    m_gestures.erase(it);
    const Action* a = find(id);
    if (a && a->cancel) a->cancel(ActionArgs{});
    if (a) closeStep(*a);
    if (a) record(*a, logged, ActionLogEntry::Status::Cancelled);
}

bool ActionRegistry::inGesture(ActionId id) const
{
    return m_gestures.contains(id.name());
}

// ---------------------------------------------------------------- log

bool ActionRegistry::argsFit(const Action& a, std::span<const ArgSpec> specs, const ActionArgs& args)
{
    std::string why = checkArgs(specs, args);
    if (why.empty()) return true;
    Log::error("action", "%.*s refused: %s", int(a.id.name().size()), a.id.name().data(), why.c_str());   // a caller's mistake, whatever the kind
    record(a, args, ActionLogEntry::Status::Refused, std::move(why));
    return false;
}

void ActionRegistry::record(const Action& a, const ActionArgs& args, ActionLogEntry::Status status, std::string why)
{
    if (!logged(a.kind)) return;
    ActionLogEntry e{ Clock::seconds(), a.kind, a.id, args.format(), status, std::move(why) };
    // Repeated runs of a coalescing action in one interaction: one line, with the last values.
    if (a.coalesce && status == ActionLogEntry::Status::Done) {
        if (m_hasPending && m_pending.id != e.id) flushPending();
        m_pending = std::move(e);
        m_hasPending = true;
        return;
    }
    flushPending();
    write(std::move(e));
}

void ActionRegistry::flushPending()
{
    if (!m_hasPending) return;
    m_hasPending = false;
    write(std::move(m_pending));
}

void ActionRegistry::write(ActionLogEntry e)
{
    static constexpr const char* kStatus[] = { "", " (refused)", " (disabled)", " (cancelled)" };
    Log::info("action", "%8.2fs %-7s %.*s%s%s%s", e.seconds, actionKindName(e.kind), int(e.id.name().size()), e.id.name().data(),
              e.args.empty() ? "" : " ", e.args.c_str(), kStatus[int(e.status)]);
    m_log.push_back(std::move(e));
    if (m_log.size() > kLogCapacity) m_log.pop_front();
}

void ActionRegistry::endFrame(bool interacting)
{
    if (interacting) return;
    flushPending();
    // Nothing held, yet a gesture is open: whatever was to end it never came (its panel closed
    // mid-drag). Cancelled, so its undo step closes.
    std::vector<ActionId> stuck;
    for (const auto& [name, args] : m_gestures) stuck.push_back(ActionId(name));
    for (const ActionId id : stuck) cancelGesture(id);
}

void ActionRegistry::setUndoSteps(std::function<void(const Action&)> open, std::function<void()> close)
{
    m_openStep  = std::move(open);
    m_closeStep = std::move(close);
}

bool ActionRegistry::isStep(const Action& a) const
{
    return a.undoStep && (a.kind == ActionKind::DocumentEdit || a.kind == ActionKind::Gesture);
}

// ---------------------------------------------------------------- shortcuts

void ActionRegistry::claimKeyboard(ActionScope scope, KeyboardClaim claim)
{
    if (m_claimed && claim <= m_claim) return;
    m_claimScope = scope;
    m_claim      = claim;
    m_claimed    = true;
}

void ActionRegistry::dispatch(const InputFrame& input, bool typing)
{
    const ActionScope panel = m_claimed ? m_claimScope : ActionScope::Global;
    m_claimed = false;   // claimed afresh while this frame draws
    if (typing || input.keys.empty()) return;
    // By id: a handler may add actions (the vector may grow under a reference).
    std::vector<ActionId> fired;
    for (const Action& a : m_actions) {
        if ((a.scope != ActionScope::Global && a.scope != panel) || !a.run) continue;
        for (const Shortcut& s : a.shortcuts)
            if (input.pressed(s, a.repeats)) {
                fired.push_back(a.id);
                break;
            }
    }
    for (const ActionId id : fired) run(id);
}

std::string ActionRegistry::shortcutLabel(ActionId id) const
{
    const Action* a = find(id);
    if (!a) return {};
    return a->shortcuts.empty() ? a->mouse : ::shortcutLabel(a->shortcuts.front());
}

std::string ActionRegistry::describe(std::initializer_list<HelpPart> parts) const
{
    std::string out;
    for (const HelpPart& part : parts) {
        if (const auto* text = std::get_if<std::string_view>(&part)) {
            out += *text;
        } else if (const auto* id = std::get_if<ActionId>(&part)) {
            out += shortcutLabel(*id);
        } else {
            std::string mod = modifiersLabel(ModPrimary);
            mod.pop_back();   // its "+"
            out += mod;
        }
    }
    return out;
}

std::string modifiersLabel(uint8_t mods)
{
    std::string out;
    if (mods & ModCtrl)  out += "Ctrl+";
    if (mods & ModAlt)   out += "Alt+";
    if (mods & ModShift) out += "Shift+";
#if defined(__APPLE__)
    if (mods & ModSuper) out += "Cmd+";
#else
    if (mods & ModSuper) out += "Super+";
#endif
    return out;
}

std::string shortcutLabel(const Shortcut& s)
{
    return modifiersLabel(s.mods) + SDL_GetKeyName(s.key);
}

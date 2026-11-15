#include "command/actions.hpp"
#include "base/clock.hpp"
#include "base/log.hpp"

#include <algorithm>
#include <cassert>

namespace {

constexpr ArgKey<int64_t> kMade{ "made" };   // what an action made (a new node's id), in the log

bool logged(ActionKind kind) { return kind == ActionKind::ProjectEdit || kind == ActionKind::Gesture || kind == ActionKind::AppState; }

// "Ctrl+Shift+" (KeyMod flags; Cmd on macOS)
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

}

const char* actionKindName(ActionKind kind)
{
    switch (kind) {
    case ActionKind::ProjectEdit: return "edit";
    case ActionKind::Gesture:      return "gesture";
    case ActionKind::AppState:     return "app";
    case ActionKind::Interface:    return "interface";
    case ActionKind::Unset:        break;
    }
    return "?";
}

// ---------------------------------------------------------------- registry

ActionRegistry::ActionRegistry() = default;

std::string ActionRegistry::malformed(const Action& action) const
{
    if (action.id.name().empty() || own(action.id)) return "its id is empty or taken";
    if (action.kind == ActionKind::Unset) return "it doesn't say its kind";
    const bool gesture = action.kind == ActionKind::Gesture;
    const bool handlers = action.begin || action.update || action.end || action.cancel;
    if (action.polled ? (gesture || action.run || handlers) : gesture ? (!action.begin || action.run) : (!action.run || handlers))
        return "a gesture begins (and never runs), a polled action has no handler, anything else runs";
    if (!gesture && (!action.endArgs.empty() || !action.updateArgs.empty())) return "only a gesture ends and updates";
    if (!action.shortcuts.empty()) return "its keys come from the keymap (bind)";
    return {};
}

void ActionRegistry::add(Action action)
{
    if (const std::string error = malformed(action); !error.empty()) {
        Log::error("action", "%.*s not added: %s", int(action.id.name().size()), action.id.name().data(), error.c_str());
        assert(false && "a malformed action");
        return;
    }
    m_index.emplace(action.id.name(), m_actions.size());
    m_actions.push_back(std::move(action));
}

bool ActionRegistry::bind(ActionId id, Shortcut shortcut)
{
    const Action* a = find(id);
    std::string error;
    if (!a) error = "no such action";
    else if (shortcut.key == Key::Unknown) error = "no key";
    else if (!a->run && !a->polled) error = "a gesture isn't started by a key";
    else {
        // Panel scopes are never live together, but the global scope is live with either.
        const ActionScope scope = a->scope;
        const auto clash = [&](ActionScope other) { return other == scope || other == ActionScope::Global || scope == ActionScope::Global; };
        for (const Action& other : m_actions)
            if (clash(other.scope))
                for (const Shortcut& o : other.shortcuts)
                    if (shortcut == o) error = "the key is " + std::string(other.id.name()) + "'s";
        for (const DelegatedKey& k : m_delegatedKeys)
            if (const Action* other = find(k.id); other && k.shortcut == shortcut && clash(other->scope))
                error = "the key is " + std::string(k.id.name()) + "'s";
    }
    if (!error.empty()) {
        Log::error("action", "%.*s: key %s not bound: %s", int(id.name().size()), id.name().data(), ::shortcutLabel(shortcut).c_str(), error.c_str());
        assert(false && "a keymap mistake");
        return false;
    }
    if (own(id)) m_actions[m_index.find(id.name())->second].shortcuts.push_back(shortcut);
    else         m_delegatedKeys.push_back({ id, shortcut });
    return true;
}

const Action* ActionRegistry::own(ActionId id) const
{
    const auto it = m_index.find(id.name());
    return it == m_index.end() ? nullptr : &m_actions[it->second];
}

const Action* ActionRegistry::find(ActionId id) const
{
    if (const Action* a = own(id)) return a;
    return m_delegate ? m_delegate->find(id) : nullptr;
}

ActionRegistry* ActionRegistry::handler(ActionId id)
{
    if (own(id)) return this;
    return m_delegate && m_delegate->find(id) ? m_delegate : nullptr;
}

bool ActionRegistry::anyGesture() const
{
    return !m_gestures.empty() || (m_delegate && m_delegate->anyGesture());
}

ActionResult ActionRegistry::run(ActionId id, ActionArgs args)
{
    if (ActionRegistry* h = handler(id); h && h != this) return h->run(id, std::move(args));
    const Action* a = find(id);
    assert(a && "unknown action");
    if (!a || !a->run) return {};
    if (!a->isEnabled()) {
        record(*a, args, ActionLogEntry::Status::Disabled);
        return {};
    }
    if (!argsFit(*a, a->args, a->payload, args)) return {};
    openStep(*a);
    const ActionResult r = a->run(args);
    closeStep(*a);
    args.payload.reset();   // never logged
    if (r.made) args.add({ kMade, r.made });
    record(*a, args, r ? ActionLogEntry::Status::Done : ActionLogEntry::Status::Refused, r.error);
    return r;
}

bool ActionRegistry::beginGesture(ActionId id, ActionArgs args)
{
    if (ActionRegistry* h = handler(id); h && h != this) return h->beginGesture(id, std::move(args));
    const Action* a = find(id);
    assert(a && a->begin && "unknown gesture");
    if (!a || !a->begin) return false;
    if (inGesture(id)) cancelGesture(id);   // never ended: the new one replaces it
    if (!a->isEnabled()) {
        record(*a, args, ActionLogEntry::Status::Disabled);
        return false;
    }
    if (!argsFit(*a, a->args, a->payload, args)) return false;
    openStep(*a);
    if (const ActionResult r = a->begin(args); !r) {
        closeStep(*a);
        record(*a, args, ActionLogEntry::Status::Refused, r.error);
        return false;
    }
    args.payload.reset();   // only for the handler
    m_gestures[id.name()] = { id, std::move(args) };
    return true;
}

void ActionRegistry::updateGesture(ActionId id, const ActionArgs& args)
{
    if (ActionRegistry* h = handler(id); h && h != this) return h->updateGesture(id, args);
    if (!inGesture(id)) return;
    const Action* a = find(id);
    if (!a || !a->update || !argsFit(*a, a->updateArgs, a->updatePayload, args)) return;   // a mistaken update is refused (logged), the gesture goes on
    a->update(args);
}

ActionResult ActionRegistry::endGesture(ActionId id, ActionArgs args)
{
    if (ActionRegistry* h = handler(id); h && h != this) return h->endGesture(id, std::move(args));
    auto it = m_gestures.find(id.name());
    if (it == m_gestures.end()) return {};
    ActionArgs logged = std::move(it->second.args);
    m_gestures.erase(it);
    const Action* a = find(id);
    if (!a) return {};
    if (!argsFit(*a, a->endArgs, nullptr, args)) {   // ends all the same: as if cancelled
        if (a->cancel) a->cancel(ActionArgs{});
        closeStep(*a);
        return {};
    }
    // The end sees the whole gesture's arguments: begin's, with the end's on top.
    ActionArgs all = logged;
    all.append(args);
    all.payload = args.payload;
    const ActionResult r = a->end ? a->end(all) : ActionResult{ true };
    closeStep(*a);
    logged.append(args);
    if (r.made) logged.add({ kMade, r.made });
    record(*a, logged, r ? ActionLogEntry::Status::Done : ActionLogEntry::Status::Refused, r.error);
    return r;
}

void ActionRegistry::cancelGesture(ActionId id)
{
    if (ActionRegistry* h = handler(id); h && h != this) return h->cancelGesture(id);
    auto it = m_gestures.find(id.name());
    if (it == m_gestures.end()) return;
    const ActionArgs logged = std::move(it->second.args);
    m_gestures.erase(it);
    const Action* a = find(id);
    if (!a) return;
    if (a->cancel) a->cancel(ActionArgs{});
    closeStep(*a);
    record(*a, logged, ActionLogEntry::Status::Cancelled);
}

void ActionRegistry::delegateTo(ActionRegistry* other)
{
    if (m_delegate && m_delegate != other) {
        std::vector<ActionId> open;
        for (const auto& [name, g] : m_delegate->m_gestures) open.push_back(g.id);
        for (const ActionId id : open) m_delegate->cancelGesture(id);
    }
    m_delegate = other;
}

bool ActionRegistry::inGesture(ActionId id) const
{
    if (!own(id) && m_delegate) return m_delegate->inGesture(id);
    return m_gestures.contains(id.name());
}

// ---------------------------------------------------------------- log

bool ActionRegistry::argsFit(const Action& a, std::span<const ArgSpec> specs, const std::type_info* payload, const ActionArgs& args)
{
    std::string error = checkArgs(specs, args);
    if (error.empty()) error = checkPayload(payload, args.payload);
    if (error.empty()) return true;
    Log::error("action", "%.*s refused: %s", int(a.id.name().size()), a.id.name().data(), error.c_str());   // a caller's mistake, whatever the kind
    record(a, args, ActionLogEntry::Status::Refused, std::move(error));
    return false;
}

void ActionRegistry::record(const Action& a, const ActionArgs& args, ActionLogEntry::Status status, std::string error)
{
    if (!logged(a.kind)) return;
    ActionLogEntry e{ Clock::sinceStart(), a.kind, a.id, args.format(), status, std::move(error) };
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
    Log::info("action", "%8.2fs %-7s %.*s%s%s%s%s%s", e.at.count(), actionKindName(e.kind), int(e.id.name().size()), e.id.name().data(),
              e.args.empty() ? "" : " ", e.args.c_str(), kStatus[int(e.status)], e.error.empty() ? "" : ": ", e.error.c_str());
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
    for (const auto& [name, open] : m_gestures) stuck.push_back(open.id);
    for (const ActionId id : stuck) cancelGesture(id);
    if (m_delegate) m_delegate->endFrame(interacting);
}

void ActionRegistry::setUndoSteps(std::function<void(const Action&)> open, std::function<void()> close)
{
    m_openStep  = std::move(open);
    m_closeStep = std::move(close);
}

bool ActionRegistry::isStep(const Action& a) const
{
    return a.undoStep && (a.kind == ActionKind::ProjectEdit || a.kind == ActionKind::Gesture);
}

// ---------------------------------------------------------------- shortcuts

void ActionRegistry::dispatch(const InputFrame& input, ActionScope panel)
{
    if (input.keys.empty()) return;
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
    for (const DelegatedKey& k : m_delegatedKeys) {   // the delegate's, as it is now
        const Action* a = find(k.id);
        if (a && (a->scope == ActionScope::Global || a->scope == panel) && input.pressed(k.shortcut, a->repeats) &&
            std::find(fired.begin(), fired.end(), k.id) == fired.end())
            fired.push_back(k.id);
    }
    for (const ActionId id : fired) run(id);
}

template<class F>
bool ActionRegistry::anyKey(ActionId id, F&& f) const
{
    if (const Action* a = own(id))
        return std::any_of(a->shortcuts.begin(), a->shortcuts.end(), f);
    return std::any_of(m_delegatedKeys.begin(), m_delegatedKeys.end(), [&](const DelegatedKey& k) { return k.id == id && f(k.shortcut); });
}

bool ActionRegistry::pressed(ActionId id, const InputFrame& input) const
{
    const Action* a = find(id);
    const bool repeats = a && a->repeats;
    return anyKey(id, [&](const Shortcut& s) { return input.pressed(s, repeats); });
}

bool ActionRegistry::held(ActionId id, const InputFrame& input) const
{
    return anyKey(id, [&](const Shortcut& s) { return input.isDown(s.key); });
}

std::string ActionRegistry::shortcutLabel(ActionId id) const
{
    const Action* a = find(id);
    if (!a) return {};
    if (!own(id))
        for (const DelegatedKey& k : m_delegatedKeys)
            if (k.id == id) return ::shortcutLabel(k.shortcut);
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

std::string shortcutLabel(const Shortcut& s)
{
    return modifiersLabel(s.mods) + keyName(s.key);
}

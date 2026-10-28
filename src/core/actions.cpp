#include "actions.hpp"

#include <SDL3/SDL_keyboard.h>
#include <cassert>
#include <chrono>
#include <cstdio>

namespace {

[[maybe_unused]] bool sameShortcut(const Shortcut& a, const Shortcut& b) { return a.key == b.key && a.mods == b.mods; }

double nowSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool logged(ActionKind kind) { return kind == ActionKind::DocumentEdit || kind == ActionKind::Gesture || kind == ActionKind::AppState; }

void formatValue(std::string& out, const ActionValue& v)
{
    char buf[32];
    if (const int64_t* i = std::get_if<int64_t>(&v)) {
        std::snprintf(buf, sizeof buf, "%lld", (long long)*i);
        out += buf;
    } else if (const double* d = std::get_if<double>(&v)) {
        std::snprintf(buf, sizeof buf, "%g", *d);
        out += buf;
    } else if (const bool* b = std::get_if<bool>(&v)) {
        out += *b ? "on" : "off";
    } else if (const std::string* s = std::get_if<std::string>(&v)) {
        const bool quote = s->empty() || s->find(' ') != std::string::npos;
        if (quote) out += '"';
        out += *s;
        if (quote) out += '"';
    } else if (const auto* list = std::get_if<std::vector<int64_t>>(&v)) {
        out += '[';
        for (size_t k = 0; k < list->size(); ++k) {
            if (k) out += ',';
            std::snprintf(buf, sizeof buf, "%lld", (long long)(*list)[k]);
            out += buf;
        }
        out += ']';
    }
}

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

// ---------------------------------------------------------------- arguments

ActionArgs& ActionArgs::add(ActionArg arg)
{
    for (ActionArg& a : m_args)
        if (a.name == arg.name) { a.value = std::move(arg.value); return *this; }
    m_args.push_back(std::move(arg));
    return *this;
}

const ActionValue* ActionArgs::find(std::string_view name) const
{
    for (const ActionArg& a : m_args)
        if (a.name == name) return &a.value;
    return nullptr;
}

int64_t ActionArgs::integer(std::string_view name, int64_t fallback) const
{
    const ActionValue* v = find(name);
    if (!v) return fallback;
    if (const int64_t* i = std::get_if<int64_t>(v)) return *i;
    if (const double* d = std::get_if<double>(v)) return int64_t(*d);
    if (const bool* b = std::get_if<bool>(v)) return *b;
    return fallback;
}

double ActionArgs::number(std::string_view name, double fallback) const
{
    const ActionValue* v = find(name);
    if (!v) return fallback;
    if (const double* d = std::get_if<double>(v)) return *d;
    if (const int64_t* i = std::get_if<int64_t>(v)) return double(*i);
    return fallback;
}

bool ActionArgs::flag(std::string_view name, bool fallback) const
{
    const ActionValue* v = find(name);
    if (!v) return fallback;
    if (const bool* b = std::get_if<bool>(v)) return *b;
    if (const int64_t* i = std::get_if<int64_t>(v)) return *i != 0;
    return fallback;
}

std::string ActionArgs::text(std::string_view name) const
{
    const ActionValue* v = find(name);
    const std::string* s = v ? std::get_if<std::string>(v) : nullptr;
    return s ? *s : std::string();
}

std::vector<int64_t> ActionArgs::list(std::string_view name) const
{
    const ActionValue* v = find(name);
    const auto* l = v ? std::get_if<std::vector<int64_t>>(v) : nullptr;
    return l ? *l : std::vector<int64_t>();
}

std::string ActionArgs::format() const
{
    std::string out;
    for (const ActionArg& a : m_args) {
        if (!out.empty()) out += ' ';
        out += a.name;
        out += '=';
        formatValue(out, a.value);
    }
    return out;
}

void ActionArgs::append(const ActionArgs& more)
{
    for (const ActionArg& a : more.m_args) add(a);
}

// ---------------------------------------------------------------- registry

ActionRegistry::ActionRegistry() : m_start(nowSeconds()) {}

void ActionRegistry::add(Action action)
{
    assert(!find(action.id) && "action ids are unique");
    assert(action.kind != ActionKind::Unset && "every action says its kind");
    assert((action.kind == ActionKind::Gesture) == bool(action.begin) && "gestures begin, the rest run");
#ifndef NDEBUG
    // Panel scopes are never live together, but the global scope is live with either.
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

ActionResult ActionRegistry::run(std::string_view id, ActionArgs args)
{
    const Action* a = find(id);
    assert(a && "unknown action");
    if (!a || !a->run) return {};
    if (!a->isEnabled()) {
        record(*a, args, ActionLogEntry::Status::Disabled);
        return {};
    }
    openStep(*a);
    const ActionResult r = a->run(args);
    closeStep(*a);
    if (r.made) args.add({ "made", r.made });
    record(*a, args, r ? ActionLogEntry::Status::Done : ActionLogEntry::Status::Refused);
    return r;
}

bool ActionRegistry::beginGesture(std::string_view id, ActionArgs args)
{
    const Action* a = find(id);
    assert(a && a->begin && "unknown gesture");
    if (!a || !a->begin) return false;
    if (inGesture(id)) cancelGesture(id);   // never ended: the new one replaces it
    if (!a->isEnabled()) {
        record(*a, args, ActionLogEntry::Status::Disabled);
        return false;
    }
    openStep(*a);
    if (!a->begin(args)) {
        closeStep(*a);
        record(*a, args, ActionLogEntry::Status::Refused);
        return false;
    }
    args.payload.reset();   // only for the handler
    m_gestures[std::string(id)] = std::move(args);
    return true;
}

void ActionRegistry::updateGesture(std::string_view id, const ActionArgs& args)
{
    if (!inGesture(id)) return;
    if (const Action* a = find(id); a && a->update) a->update(args);
}

ActionResult ActionRegistry::endGesture(std::string_view id, ActionArgs args)
{
    auto it = m_gestures.find(id);
    if (it == m_gestures.end()) return {};
    ActionArgs logged = std::move(it->second);
    m_gestures.erase(it);
    const Action* a = find(id);
    const ActionResult r = a && a->end ? a->end(args) : ActionResult{ true };
    if (a) closeStep(*a);
    logged.append(args);
    if (r.made) logged.add({ "made", r.made });
    if (a) record(*a, logged, r ? ActionLogEntry::Status::Done : ActionLogEntry::Status::Refused);
    return r;
}

void ActionRegistry::cancelGesture(std::string_view id)
{
    auto it = m_gestures.find(id);
    if (it == m_gestures.end()) return;
    const ActionArgs logged = std::move(it->second);
    m_gestures.erase(it);
    const Action* a = find(id);
    if (a && a->cancel) a->cancel(ActionArgs{});
    if (a) closeStep(*a);
    if (a) record(*a, logged, ActionLogEntry::Status::Cancelled);
}

bool ActionRegistry::inGesture(std::string_view id) const
{
    return m_gestures.contains(id);
}

// ---------------------------------------------------------------- log

void ActionRegistry::record(const Action& a, const ActionArgs& args, ActionLogEntry::Status status)
{
    if (!logged(a.kind)) return;
    ActionLogEntry e{ nowSeconds() - m_start, a.kind, a.id, args.format(), status };
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
    std::fprintf(stderr, "[action %8.2fs] %-7s %s%s%s%s\n", e.seconds, actionKindName(e.kind), e.id.c_str(),
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
    std::vector<std::string> stuck;
    for (const auto& [id, args] : m_gestures) stuck.push_back(id);
    for (const std::string& id : stuck) cancelGesture(id);
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
    std::vector<std::string> fired;
    for (const Action& a : m_actions) {
        if ((a.scope != ActionScope::Global && a.scope != panel) || !a.run) continue;
        for (const Shortcut& s : a.shortcuts)
            if (input.pressed(s, a.repeats)) {
                fired.push_back(a.id);
                break;
            }
    }
    for (const std::string& id : fired) run(id);
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

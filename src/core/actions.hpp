#pragma once

#include "core/input.hpp"

#include <absl/container/flat_hash_map.h>
#include <any>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

enum class ActionScope : uint8_t {
    Global,
    Canvas,
    Graph,
};

enum class ActionKind : uint8_t {
    Unset,          // every action says which it is
    DocumentEdit,
    Gesture,
    AppState,
    Interface,
};
const char* actionKindName(ActionKind kind);   // "edit", "gesture", "app", "interface"

enum class KeyboardClaim : uint8_t {
    Focus,     // the panel's window is focused
    Pointer,   // the pointer is over the panel (or held on it)
};

// One named argument. Names are literals; values are numbers, flags, text or a list of ids.
using ActionValue = std::variant<int64_t, double, bool, std::string, std::vector<int64_t>>;
struct ActionArg {
    std::string_view name;
    ActionValue      value;

    template<class T>
    ActionArg(std::string_view n, const T& v) : name(n), value(toValue(v)) {}

private:
    template<class T>
    static ActionValue toValue(const T& v)
    {
        if constexpr (std::is_same_v<T, bool>)                                    return v;
        else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)            return int64_t(v);
        else if constexpr (std::is_floating_point_v<T>)                           return double(v);
        else if constexpr (std::is_convertible_v<const T&, std::string_view>)     return std::string(std::string_view(v));
        else return std::vector<int64_t>(v.begin(), v.end());   // a list of ids
    }
};

// An action's arguments, written out in the log ("from=12 to=7 slot=layer:2"), plus an optional
// payload too big or too typed for the log (a stroke's points, an image's tiles).
class ActionArgs {
public:
    ActionArgs() = default;
    ActionArgs(std::initializer_list<ActionArg> args) : m_args(args) {}

    ActionArgs& add(ActionArg arg);   // replaces one of the same name
    bool has(std::string_view name) const { return find(name) != nullptr; }
    int64_t     integer(std::string_view name, int64_t fallback = 0) const;
    double      number(std::string_view name, double fallback = 0.0) const;   // an integer reads too
    bool        flag(std::string_view name, bool fallback = false) const;
    std::string text(std::string_view name) const;
    std::vector<int64_t> list(std::string_view name) const;

    std::any payload;
    template<class T> const T* payloadAs() const { return std::any_cast<T>(&payload); }

    std::string format() const;   // as the log writes them
    void append(const ActionArgs& more);   // `more`'s values win

private:
    const ActionValue* find(std::string_view name) const;
    std::vector<ActionArg> m_args;
};

// What running an action did: done or refused, and what it made (a new node's id).
struct ActionResult {
    bool    done = false;
    int64_t made = 0;
    explicit operator bool() const { return done; }
};

// A handler: takes the arguments (or none), returns an ActionResult, a bool (done), or nothing (done).
class ActionHandler {
public:
    ActionHandler() = default;
    template<class F>
        requires std::is_invocable_v<F&, const ActionArgs&> || std::is_invocable_v<F&>
    ActionHandler(F f) : m_f(wrap(std::move(f))) {}

    ActionResult operator()(const ActionArgs& args) const { return m_f(args); }
    explicit operator bool() const { return bool(m_f); }

private:
    template<class F>
    static std::function<ActionResult(const ActionArgs&)> wrap(F f)
    {
        return [f = std::move(f)](const ActionArgs& args) mutable -> ActionResult {
            const auto call = [&]() -> decltype(auto) {
                if constexpr (std::is_invocable_v<F&, const ActionArgs&>) return f(args);
                else return f();
            };
            using R = decltype(call());
            if constexpr (std::is_void_v<R>) { call(); return { true }; }
            else if constexpr (std::is_same_v<R, bool>) return { call() };
            else return call();
        };
    }
    std::function<ActionResult(const ActionArgs&)> m_f;
};

struct Action {
    std::string id{};                    // "group.name": stable, what menus and tooltips refer to
    std::string title{};                 // as menus show it
    ActionKind  kind = ActionKind::Unset;
    std::vector<Shortcut> shortcuts{};   // the first is the one shown; none: menus only
    ActionScope scope = ActionScope::Global;
    bool repeats  = false;               // fires again while its keys are held (the OS's key repeat)
    bool coalesce = false;               // AppState: runs in one interaction (a slider drag, a held key) log once
    bool undoStep = true;                // DocumentEdit / Gesture: one undo step (off: undo, redo, history jumps)
    std::function<bool()> enabled{};     // null: always
    std::function<bool()> checked{};     // a toggle, shown with a check mark (null: not one)
    ActionHandler run{};                 // a command: everything at once
    ActionHandler begin{}, update{}, end{}, cancel{};   // a gesture: across frames (update is never logged)

    bool isEnabled() const { return !enabled || enabled(); }
};

// One line of the log.
struct ActionLogEntry {
    enum class Status : uint8_t { Done, Refused, Disabled, Cancelled };
    double      seconds = 0.0;   // since the registry was made
    ActionKind  kind = ActionKind::Unset;
    std::string id;
    std::string args;            // as written
    Status      status = Status::Done;
};

class ActionRegistry {
public:
    ActionRegistry();

    void add(Action action);   // ids are unique; a shortcut means one thing wherever it can fire
    const Action* find(std::string_view id) const;

    // A command: runs and logs it. Refused (not done) if unknown or disabled.
    ActionResult run(std::string_view id, ActionArgs args = {});

    // A gesture: begin() (false if unknown, disabled or refused), update() as often as needed,
    // then end() or cancel(). Logged once, when it ends: begin's arguments, then end's.
    bool beginGesture(std::string_view id, ActionArgs args = {});
    void updateGesture(std::string_view id, const ActionArgs& args);
    ActionResult endGesture(std::string_view id, ActionArgs args = {});
    void cancelGesture(std::string_view id);
    bool inGesture(std::string_view id) const;

    // Called around every DocumentEdit and Gesture that's an undo step: open before it runs (or
    // begins), close after it ran (or ended, was cancelled, or refused to begin).
    void setUndoSteps(std::function<void(const Action&)> open, std::function<void()> close);

    void claimKeyboard(ActionScope scope, KeyboardClaim claim);   // the first of the strongest wins
    // Runs the enabled actions whose shortcut went down this frame: global ones, and those of the
    // scope claimed last frame. Once a frame, before the panels draw.
    void dispatch(const InputFrame& input, bool typing);
    // Once a frame, after the panels: `interacting` while a control is held or a key is down
    // (coalesced app-state runs are logged once that ends; a gesture still open then lost its
    // end, and is cancelled).
    void endFrame(bool interacting);

    std::string shortcutLabel(std::string_view id) const;   // "Ctrl+Shift+Z" ("" if none)
    const std::vector<Action>& actions() const { return m_actions; }
    const std::deque<ActionLogEntry>& log() const { return m_log; }

private:
    static constexpr size_t kLogCapacity = 2000;

    void record(const Action& a, const ActionArgs& args, ActionLogEntry::Status status);
    bool isStep(const Action& a) const;
    void openStep(const Action& a)  { if (isStep(a) && m_openStep) m_openStep(a); }
    void closeStep(const Action& a) { if (isStep(a) && m_closeStep) m_closeStep(); }
    void write(ActionLogEntry entry);
    void flushPending();

    std::vector<Action> m_actions;
    absl::flat_hash_map<std::string, ActionArgs> m_gestures;   // in progress: id -> begin's arguments
    std::deque<ActionLogEntry> m_log;
    std::function<void(const Action&)> m_openStep;
    std::function<void()>              m_closeStep;
    ActionLogEntry m_pending;          // a coalescing run, logged once its interaction ends
    bool           m_hasPending = false;
    double         m_start = 0.0;      // steady clock, seconds
    ActionScope   m_claimScope = ActionScope::Global;   // for the next dispatch
    KeyboardClaim m_claim      = KeyboardClaim::Focus;
    bool          m_claimed    = false;
};

std::string shortcutLabel(const Shortcut& s);   // Cmd on macOS

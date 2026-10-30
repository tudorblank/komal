#pragma once
// An action: one named thing a user can do, with how it's triggered, logged and run. The
// registry (actions.hpp) holds them all.

#include "command/action-args.hpp"
#include "command/action-ids.hpp"
#include "command/input.hpp"

#include <functional>
#include <string>
#include <type_traits>
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
    ActionId    id{};                    // from command/action-ids.hpp
    std::string title{};                 // as menus show it
    ActionKind  kind = ActionKind::Unset;
    std::vector<ArgSpec> args{};         // what it takes (a gesture: what it begins with); checked before it runs
    std::vector<ArgSpec> endArgs{};      // a gesture: what it ends with (its end sees both)
    std::vector<ArgSpec> updateArgs{};   // a gesture: what each update takes (checked like the rest)
    std::vector<Shortcut> shortcuts{};   // the first is the one shown; none: menus only
    std::string mouse{};                 // how the mouse runs it where it's detected ("Double-click"): shown without a key
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

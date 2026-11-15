#pragma once

// Every action, in one registry: added once, found by id, run (or begun and ended, for a gesture)
// with checked arguments, dispatched from shortcuts, and logged by kind.
//
// A registry can hand on what it doesn't have (delegateTo): the interface's holds the app's own
// actions and hands the project's to the active project's registry, which runs, logs and
// undoes them. Keys for those are bound here, by id, so they stay whichever project is active.
//
// How the app's three kinds of registry route (ui/ui-main.cpp wires them):
//   the interface's   the app's own (quit, panels, files), every panel's and tool's; what input,
//                     menus and shortcuts run. Hands on what it doesn't have to ...
//   a project's       its workspace's edits and gestures (project/project-actions): one per
//                     project, the active one's is the delegate. Undo steps open and close here.
//   the projects'     new, open, save, activate, close (project/projects-manager-actions). They
//                     change which projects exist, so they never run mid-frame: the interface
//                     has a stand-in for each that queues the request (ActionResult::later) and
//                     runs them all between frames, reporting failures as notices.
// Keys for a project's actions are bound with a stand-in project as the delegate, before any is
// open: bind() checks a key against what it would run.

#include "base/clock.hpp"
#include "command/action.hpp"

#include <absl/container/flat_hash_map.h>
#include <deque>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct PrimaryKey {};   // in help text: the platform's command modifier
using HelpPart = std::variant<std::string_view, ActionId, PrimaryKey>;

// One line of the log.
struct ActionLogEntry {
    enum class Status : uint8_t { Done, Refused, Disabled, Cancelled };
    Clock::Seconds at{};         // since the app started (base/clock.hpp)
    ActionKind  kind = ActionKind::Unset;
    ActionId    id;
    std::string args;            // as written
    Status      status = Status::Done;
    std::string error;           // refused: what was wrong (its arguments, or the handler's reason)
};

class ActionRegistry {
public:
    ActionRegistry();

    // Checked first, in every build: a unique id, a kind, run or begin to match it. A malformed
    // one is refused (logged).
    void add(Action action);
    // A key for an action that's been added (the keymap, ui/keymap): one that means one thing
    // wherever it can fire. False (logged) if not.
    bool bind(ActionId id, Shortcut shortcut);
    const Action* find(ActionId id) const;   // its own, else the delegate's
    // Actions it doesn't have are found, run, begun and ended there (null: none). Changed between
    // frames; a gesture still open in the one it leaves can't end any more, so it's cancelled
    // (its undo step closes): the one it leaves must still be there (switch away before it goes).
    void delegateTo(ActionRegistry* other);
    bool anyGesture() const;   // here or in the delegate

    // A command: runs and logs it. Refused (not done) if unknown or disabled.
    ActionResult run(ActionId id, ActionArgs args = {});

    // A gesture: begin() (false if unknown, disabled or refused), update() as often as needed (each
    // checked against its updateArgs), then end() or cancel(). Logged once, when it ends: begin's
    // arguments, then end's.
    bool beginGesture(ActionId id, ActionArgs args = {});
    void updateGesture(ActionId id, const ActionArgs& args);
    ActionResult endGesture(ActionId id, ActionArgs args = {});
    void cancelGesture(ActionId id);
    bool inGesture(ActionId id) const;

    // Called around every ProjectEdit and Gesture that's an undo step: open before it runs (or
    // begins), close after it ran (or ended, was cancelled, or refused to begin).
    void setUndoSteps(std::function<void(const Action&)> open, std::function<void()> close);

    // Runs the enabled actions whose shortcut went down this frame: global ones, and those of
    // `scope` (the panel the keyboard belongs to: the UI decides, ui/input-owner.hpp). Once a
    // frame, before the panels draw; not while a field is being typed into.
    void dispatch(const InputFrame& input, ActionScope scope);
    // Once a frame, after the panels: `interacting` while a control is held or a key is down
    // (coalesced app-state runs are logged once that ends; a gesture still open then lost its
    // end, and is cancelled).
    void endFrame(bool interacting);

    // A polled action's keys (Action::polled), read by whoever uses it: one went down this frame
    // (exact modifiers; again with the key repeat if it repeats), or one is down now (whatever
    // modifiers: the spring-loaded hand).
    bool pressed(ActionId id, const InputFrame& input) const;
    bool held(ActionId id, const InputFrame& input) const;

    std::string shortcutLabel(ActionId id) const;   // "Ctrl+Shift+Z", else its mouse way ("" if neither)
    // Help text that names keys as they're bound: its parts in order, an action as its
    // shortcutLabel(), PrimaryKey as the platform's command modifier ("Ctrl", "Cmd").
    std::string describe(std::initializer_list<HelpPart> parts) const;
    const std::vector<Action>& actions() const { return m_actions; }
    const std::deque<ActionLogEntry>& log() const { return m_log; }

private:
    static constexpr size_t kLogCapacity = 2000;

    std::string malformed(const Action& action) const;   // what's wrong with it (empty: it may be added)
    void record(const Action& a, const ActionArgs& args, ActionLogEntry::Status status, std::string error = {});
    // Else refused, and the error says what went wrong: arguments against `specs`, the payload against `payload`.
    bool argsFit(const Action& a, std::span<const ArgSpec> specs, const std::type_info* payload, const ActionArgs& args);
    bool isStep(const Action& a) const;
    void openStep(const Action& a)  { if (isStep(a) && m_openStep) m_openStep(a); }
    void closeStep(const Action& a) { if (isStep(a) && m_closeStep) m_closeStep(); }
    void write(ActionLogEntry entry);
    void flushPending();

    const Action* own(ActionId id) const;   // this registry's only
    template<class F> bool anyKey(ActionId id, F&& f) const;   // f(shortcut) for its keys, here or delegated: any true
    ActionRegistry* handler(ActionId id);   // which one has it: this, the delegate, or null

    std::vector<Action> m_actions;                       // in the order they were added (menus, dispatch)
    ActionRegistry* m_delegate = nullptr;
    struct DelegatedKey { ActionId id; Shortcut shortcut; };
    std::vector<DelegatedKey> m_delegatedKeys;           // keys for the delegate's actions, by id
    absl::flat_hash_map<std::string_view, size_t> m_index;        // name -> m_actions index
    struct OpenGesture { ActionId id; ActionArgs args; };   // begin's arguments
    absl::flat_hash_map<std::string_view, OpenGesture> m_gestures;   // in progress, by name
    std::deque<ActionLogEntry> m_log;
    std::function<void(const Action&)> m_openStep;
    std::function<void()>              m_closeStep;
    ActionLogEntry m_pending;          // a coalescing run, logged once its interaction ends
    bool           m_hasPending = false;
};

std::string shortcutLabel(const Shortcut& s);   // Cmd on macOS

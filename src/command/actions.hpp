#pragma once

// Every action, in one registry: added once, found by id, run (or begun and ended, for a gesture)
// with checked arguments, dispatched from shortcuts, and logged by kind.

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

enum class KeyboardClaim : uint8_t {
    Focus,     // the panel's window is focused
    Pointer,   // the pointer is over the panel (or held on it)
};

// One line of the log.
struct ActionLogEntry {
    enum class Status : uint8_t { Done, Refused, Disabled, Cancelled };
    double      seconds = 0.0;   // since the app started (base/clock.hpp)
    ActionKind  kind = ActionKind::Unset;
    ActionId    id;
    std::string args;            // as written
    Status      status = Status::Done;
    std::string why;             // refused for its arguments: what was wrong
};

class ActionRegistry {
public:
    ActionRegistry();

    // Checked first, in every build: a unique id, a kind, run or begin to match it, and a shortcut
    // that means one thing wherever it can fire. A malformed one is refused (logged).
    void add(Action action);
    const Action* find(ActionId id) const;

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

    std::string shortcutLabel(ActionId id) const;   // "Ctrl+Shift+Z", else its mouse way ("" if neither)
    // Help text that names keys as they're bound: its parts in order, an action as its
    // shortcutLabel(), PrimaryKey as the platform's command modifier ("Ctrl", "Cmd").
    std::string describe(std::initializer_list<HelpPart> parts) const;
    const std::vector<Action>& actions() const { return m_actions; }
    const std::deque<ActionLogEntry>& log() const { return m_log; }

private:
    static constexpr size_t kLogCapacity = 2000;

    std::string whyNot(const Action& action) const;   // empty: it may be added
    void record(const Action& a, const ActionArgs& args, ActionLogEntry::Status status, std::string why = {});
    bool argsFit(const Action& a, std::span<const ArgSpec> specs, const ActionArgs& args);   // else refused, with why
    bool isStep(const Action& a) const;
    void openStep(const Action& a)  { if (isStep(a) && m_openStep) m_openStep(a); }
    void closeStep(const Action& a) { if (isStep(a) && m_closeStep) m_closeStep(); }
    void write(ActionLogEntry entry);
    void flushPending();

    std::vector<Action> m_actions;                       // in the order they were added (menus, dispatch)
    absl::flat_hash_map<std::string_view, size_t> m_index;        // name -> m_actions index
    absl::flat_hash_map<std::string_view, ActionArgs> m_gestures;  // in progress: name -> begin's arguments
    std::deque<ActionLogEntry> m_log;
    std::function<void(const Action&)> m_openStep;
    std::function<void()>              m_closeStep;
    ActionLogEntry m_pending;          // a coalescing run, logged once its interaction ends
    bool           m_hasPending = false;
    ActionScope   m_claimScope = ActionScope::Global;   // for the next dispatch
    KeyboardClaim m_claim      = KeyboardClaim::Focus;
    bool          m_claimed    = false;
};

std::string shortcutLabel(const Shortcut& s);   // Cmd on macOS
std::string modifiersLabel(uint8_t mods);       // "Ctrl+Shift+" (KeyMod flags; Cmd on macOS)

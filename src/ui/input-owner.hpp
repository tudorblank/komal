#pragma once
// Who the input belongs to this frame, decided in one place. ImGui knows whether a field is
// being typed into; the panels that take input of their own (the canvas, the graph) claim the
// keyboard while they draw, for their shortcuts. A claim made this frame decides whose
// shortcuts run next frame: shortcuts dispatch before the panels draw.
//
//   owner.beginFrame();                                      // UserInterface, first thing
//   if (!owner.typing()) actions.dispatch(input, owner.keyboard());
//   owner.claim(ActionScope::Canvas, InputOwner::Claim::Pointer);   // a panel, while it draws
//
// The pointer itself is ImGui's to give: a panel has it while ImGui says its item is hovered or
// held, and only then reads the InputFrame's samples (input/input-frame.hpp).

#include "command/action.hpp"

#include <cstdint>

class InputOwner {
public:
    enum class Claim : uint8_t {
        Focus,     // the panel's window is focused
        Pointer,   // the pointer is over the panel (or held on it): beats focus
    };

    void beginFrame();   // reads ImGui; last frame's claims decide this frame's keyboard

    bool typing() const { return m_typing; }                 // a field has the keys: no shortcuts, no held-key tools
    ActionScope keyboard() const { return m_keyboard; }      // whose shortcuts run (Global: nobody claimed)
    void claim(ActionScope panel, Claim how);                // the first of the strongest wins

private:
    bool        m_typing   = false;
    ActionScope m_keyboard = ActionScope::Global;
    ActionScope m_claimed  = ActionScope::Global;   // this frame's, for the next
    Claim       m_how      = Claim::Focus;
    bool        m_hasClaim = false;
};

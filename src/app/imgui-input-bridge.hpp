#pragma once
// The input's way into ImGui: its only reader is ImGui's own io (ImGui_ImplSDL3_ProcessEvent),
// so ImGui's widgets (hover, clicks, typing, docking, popups) see it. Nothing in the app reads
// from here; part of Input (app/input.hpp).
//
// ImGui keeps one state per frame, so a button pressed and released within one frame would be a
// click it never sees. Paced so nothing is lost at a low frame rate: a second change of the same
// button or key in one frame, and everything after it, waits for the next frame; a run of mouse
// moves goes in as its last.
//
//   bridge.beginFrame();   // before the frame's events: last frame's held ones go first
//   bridge.push(e);        // every event, as it is polled
//   bridge.endEvents();    // after the frame's last event

#include "base/clock.hpp"

#include <SDL3/SDL_events.h>
#include <cstdint>
#include <string>
#include <vector>

class ImGuiInputBridge {
public:
    void beginFrame();
    void push(const SDL_Event& e);
    void endEvents();

    bool holding() const { return !m_held.empty(); }        // events wait for the next frame
    Clock::TimePoint lastMove() const { return m_lastMove; }   // when the mouse last moved

private:
    static constexpr size_t kMaxHeld = 1024;   // a backlog past this (a long stall) goes in at once
    struct Held {
        SDL_Event   event;
        std::string text;   // a text event's: SDL's own is freed by the next poll
    };
    bool changedTwice(const SDL_Event& e);   // also records the change
    void hold(const SDL_Event& e);
    void flushMove();

    std::vector<Held> m_held, m_replay;        // swapped each frame, so both keep their storage
    std::vector<SDL_Scancode> m_keysChanged;   // this frame
    uint32_t  m_buttonsChanged = 0;            // this frame, a bit per SDL button
    bool      m_cut = false;                   // the rest of this frame is held back
    SDL_Event m_move{};                        // the run of mouse moves so far: its last
    bool      m_hasMove = false;
    Clock::TimePoint m_lastMove{};             // when the mouse last moved
};

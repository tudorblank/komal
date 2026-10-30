#pragma once
// ImGui's copy of the input, fed so nothing is lost at a low frame rate: a second change of the
// same button or key in one frame, and everything after it, waits for the next frame; a run of
// mouse moves goes in as its last. Also tells when ImGui will next draw something by itself.
//
//   feed.beginFrame();   // before the frame's events: last frame's held ones go first
//   feed.push(e);        // every event, as it is polled
//   feed.endEvents();    // after the frame's last event

#include "base/clock.hpp"

#include <SDL3/SDL_events.h>
#include <cstdint>
#include <string>
#include <vector>

struct InputFrame;

class ImGuiFeed {
public:
    void beginFrame();
    void push(const SDL_Event& e);
    void endEvents();
    // When ImGui next changes what it draws by itself (base/frames.hpp), from public state only:
    // now while events wait, ticking while a tooltip may still appear or the caret blinks, at the
    // key repeat rate while a key is held in a field.
    double nextFrameIn(Clock::TimePoint frameStart, const InputFrame& input) const;

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

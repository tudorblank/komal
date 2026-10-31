#pragma once
// SDL's events as InputFrames (command/input.hpp): the platform's keys, buttons and pen turned into
// the app's own. Feed it every event, in order, as it arrives.

#include "command/input.hpp"

#include <SDL3/SDL_events.h>

class InputCollector {
public:
    void beginFrame();   // before the frame's events (a frame that wasn't drawn keeps its input)
    void handle(const SDL_Event& e);
    const InputFrame& frame() const { return m_frame; }
    void consumed() { m_consumed = true; }   // the frame was drawn: its events are used up

private:
    void push(PointerEvent ev);
    void releaseAll();   // focus lost: what was held is released elsewhere, never here
    PointerEvent penEvent(PointerEvent::Type type, float x, float y, uint64_t timeNs) const;

    InputFrame m_frame;
    bool m_consumed = true;
};

Key keyFromSdl(SDL_Keycode key);

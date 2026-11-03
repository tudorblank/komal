#pragma once
// SDL events -> the InputFrame (input/input-frame.hpp): every pointer sample with pressure and
// time, keys, held buttons, the pen, in Komal's own words. The one file of input/ that knows SDL:
// it's built into the app, not the core, and only app/ includes it. ImGui gets the same events
// on its own way in (app/imgui-bridge.hpp).
//
//   input.beginFrame();
//   while (SDL_PollEvent(&e)) input.handle(e);
//   ... the UI reads input.frame() ...
//   input.consumed();   // the frame was drawn

#include "input/input-frame.hpp"

#include <SDL3/SDL_events.h>

class SdlInput {
public:
    void beginFrame();   // before the frame's events (a frame that wasn't drawn keeps its input)
    void handle(const SDL_Event& e);
    void consumed() { m_consumed = true; }   // the frame was drawn: its events are used up

    const InputFrame& frame() const { return m_frame; }

private:
    void push(PointerEvent ev);
    void releaseAll();   // focus lost: what was held is released elsewhere, never here
    PointerEvent penEvent(PointerEvent::Type type, float x, float y, uint64_t timeNs) const;

    InputFrame m_frame;
    bool       m_consumed = true;
};

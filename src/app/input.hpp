#pragma once
// The input's one front door: every platform event comes in here and goes out two ways, to two
// readers that want different things.
//
//   SDL event ─┬─ InputCollector   -> InputFrame (command/input.hpp): every pointer sample with
//              │                      pressure and time, keys, held buttons, the pen. Read by the
//              │                      UI: shortcuts, tools (a stroke wants every sample), panels.
//              └─ ImGuiInputBridge -> ImGui's io: one state per frame, paced so no click is lost.
//                                     Read by ImGui's widgets only (hover, clicks, typing).
//
// Who of the UI gets the input in a frame is the UI's to say (ui/input-owner.hpp).
//
//   input.beginFrame();
//   while (SDL_PollEvent(&e)) input.handle(e);
//   input.endEvents();
//   ... ui reads input.frame() ...
//   input.consumed();   // the frame was drawn

#include "app/imgui-input-bridge.hpp"
#include "app/input-collector.hpp"

class Input {
public:
    void beginFrame();
    void handle(const SDL_Event& e);   // to both
    void endEvents();
    void consumed() { m_collector.consumed(); }

    const InputFrame& frame() const { return m_collector.frame(); }
    bool imguiHolding() const { return m_imgui.holding(); }               // input waits for ImGui's next frame
    Clock::TimePoint lastMove() const { return m_imgui.lastMove(); }

private:
    InputCollector   m_collector;
    ImGuiInputBridge m_imgui;
};

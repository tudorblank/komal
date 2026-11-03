#pragma once
// Everything the app does with ImGui, in one place: its context and settings, its SDL and wgpu
// backends, feeding it the window's events, its frames, and when it next needs one by itself.
// The UI draws with ImGui between newFrame() and endFrame(); nothing else in app/ touches it.
//
// Events reach ImGui paced: ImGui keeps one state per frame, so a button pressed and released
// within one frame would be a click it never sees. A second change of the same button or key in
// one frame, and everything after it, waits for the next frame; a run of mouse moves goes in as
// its last.
//
//   imgui.init(window, gpu, iniPath);
//   imgui.beginEvents(); while (SDL_PollEvent(&e)) imgui.handle(e); imgui.endEvents();
//   imgui.newFrame(width, height);  ... ui.draw() ...  imgui.endFrame();
//   imgui.draw(pass);   // into the window's surface

#include "base/clock.hpp"

#include <SDL3/SDL_events.h>
#include <webgpu/webgpu.h>
#include <cstdint>
#include <string>
#include <vector>

class GpuDevice;
struct InputFrame;
struct SDL_Window;

class ImGuiBridge {
public:
    ~ImGuiBridge() { shutdown(); }

    // The context (docking, keyboard navigation, the layout saved at iniPath), the fonts, both
    // backends. After the GPU is up.
    void init(SDL_Window* window, const GpuDevice& gpu, std::string iniPath);
    void shutdown();

    // The GPU device is made again (it was lost): ImGui's renderer goes with the old one.
    void releaseGpu();
    void restoreGpu(const GpuDevice& gpu);

    // ---- events, every one as it is polled
    void beginEvents();   // before the frame's events: last frame's held ones go first
    void handle(const SDL_Event& e);
    void endEvents();     // after the frame's last event

    // ---- frames
    void newFrame(uint32_t pixelWidth, uint32_t pixelHeight);   // the surface's size
    void endFrame();                                             // what was drawn, ready
    void draw(WGPURenderPassEncoder pass);                       // ... drawn into the surface
    // When ImGui next changes what it draws by itself, without new input (base/frames.hpp): now
    // while events wait for it, ticking while a tooltip may appear or the caret blinks, at the
    // key repeat rate while a key is held in a field.
    Clock::Seconds nextFrameIn(const InputFrame& input, Clock::TimePoint frameStart) const;

private:
    static constexpr size_t kMaxHeld = 1024;   // a backlog past this (a long stall) goes in at once
    struct Held {
        SDL_Event   event;
        std::string text;   // a text event's: SDL's own is freed by the next poll
    };
    void initRenderer(const GpuDevice& gpu);
    bool changedTwice(const SDL_Event& e);   // also records the change
    void hold(const SDL_Event& e);
    void flushMove();

    bool        m_ready = false;      // context and backends up
    bool        m_renderer = false;   // its wgpu backend up (not while the device is lost)
    std::string m_iniPath;            // ImGui keeps a pointer to it

    std::vector<Held> m_held, m_replay;        // swapped each frame, so both keep their storage
    std::vector<SDL_Scancode> m_keysChanged;   // this frame
    uint32_t  m_buttonsChanged = 0;            // this frame, a bit per SDL button
    bool      m_cut = false;                   // the rest of this frame is held back
    SDL_Event m_move{};                        // the run of mouse moves so far: its last
    bool      m_hasMove = false;
    Clock::TimePoint m_lastMove{};             // when the mouse last moved
};

#pragma once
#include "core/input.hpp"
#include "core/project.hpp"
#include "gfx/device.hpp"
#include "ui/ui-main.hpp"

#include <SDL3/SDL_events.h>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

struct SDL_Window;

class Application {
public:
    static constexpr double kNoFrame = std::numeric_limits<double>::infinity();
    static void wakeMainLoop();   // any thread: render a frame soon, even if the loop is asleep

    ~Application();

    bool init(const char* title, int width, int height);
    int exec(); // runs the main loop, returns exit code
    void openFile(const char* path) { m_userInterface.openFile(path); } // imported once running

private:
    // window
    bool m_running = false;
    SDL_Window* m_window = nullptr;
    void shutdown();

    class ImGuiFeed {
    public:
        void beginFrame();               // before the frame's events: last frame's held ones go first
        void push(const SDL_Event& e);   // every event, as it is polled
        void endEvents();                // after the frame's last event
        bool holding() const { return !m_held.empty(); }   // events wait for the next frame

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
    };

    // input / events: every SDL event goes to all three, as it is polled (see core/input.hpp)
    InputCollector m_input;       // raw: every pointer sample and key, for tools and shortcuts
    ImGuiFeed      m_imguiFeed;   // ImGui's copy
    bool processEvents();         // true if any arrived
    void handleEvent(const SDL_Event& e);   // the app's own: quit, scale, size, drops

    // internals
    GpuDevice m_gpu;
    Project m_project; // for canvas + graph
    
    // imgui
    UserInterface m_userInterface;
    std::string m_prefDir;   // where settings live ("" if unknown: the working directory)
    std::string m_iniPath;   // layout file
    int m_normalW = 0, m_normalH = 0;   // the window's size when not maximized
    bool m_imguiReady = false;

    // render
    void applyContentScale();
    float m_contentScale = 0;
    void renderFrame();
    
    float m_acquireMs = 0, m_cpuMs = 0;

    // frames (see the top). What input changes can take a few frames to show (a popup measures itself hidden first,
    // a field focuses a frame after it opens, hover follows a layout change a frame late).
    static constexpr int kFramesAfterInput = 3;
    static constexpr int kNoImageRetryMs   = 100; // between tries while the surface gives no image
    int      m_framesAfterInput = kFramesAfterInput;   // still to render (the first frames too)
    uint64_t m_lastMoveNs = 0;                         // SDL time of the last mouse move
    uint64_t m_frameStartNs = 0;                       // what nextFrameIn() counts from
    bool     m_noImage = false;                        // the last frame got no swapchain image
    double nextFrameIn() const;
    void   waitForFrame();
};
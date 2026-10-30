#pragma once
#include "app/imgui-feed.hpp"
#include "base/clock.hpp"
#include "base/frames.hpp"
#include "command/input.hpp"
#include "document/project.hpp"
#include "gfx/device.hpp"
#include "ui/ui-main.hpp"

#include <SDL3/SDL_events.h>
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

// Runs the frames (base/frames.hpp): draws on demand, and installs the main loop's waker.
class Application {
public:
    ~Application();

    bool init(const char* title, int width, int height);
    int exec(); // runs the main loop, returns exit code
    void openFile(const char* path) { m_userInterface.openFile(path); } // imported once running

private:
    bool m_running = false;
    SDL_Window* m_window = nullptr;
    void shutdown();

    // input / events: every SDL event goes to all three, as it is polled (see command/input.hpp)
    InputCollector m_input;       // raw: every pointer sample and key, for tools and shortcuts
    ImGuiFeed      m_imguiFeed;   // ImGui's copy (app/imgui-feed.hpp)
    bool processEvents();         // true if any arrived
    void handleEvent(const SDL_Event& e);   // the app's own: quit, scale, size, drops

    GpuDevice m_gpu;
    Project m_project;

    UserInterface m_userInterface;
    HostInfo      m_host{ .window = nullptr, .uiScale = 0.0f, .gpu = {} };   // what the interface reads about the window; scale 0: not styled yet
    std::string m_prefDir;   // where settings live ("" if unknown: the working directory)
    std::string m_iniPath;   // layout file
    int m_normalW = 0, m_normalH = 0;   // the window's size when not maximized
    bool m_imguiReady = false;

    void applyContentScale();
    void renderFrame();

    // frames (see the top). What input changes can take a few frames to show (a popup measures itself hidden first,
    // a field focuses a frame after it opens, hover follows a layout change a frame late).
    static constexpr int kFramesAfterInput = 3;
    static constexpr int kNoImageRetryMs   = 100; // between tries while the surface gives no image
    int      m_framesAfterInput = kFramesAfterInput;   // still to render (the first frames too)
    Clock::TimePoint m_frameStart{};                   // what nextFrameIn() counts from
    bool     m_noImage = false;                        // the last frame got no swapchain image
    double nextFrameIn() const;
    void   waitForFrame();
};
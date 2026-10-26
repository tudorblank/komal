#pragma once
#include "core/input.hpp"
#include "core/system-cursors.hpp"
#include "core/project.hpp"
#include "gfx/device.hpp"
#include "ui/ui-main.hpp"

#include <SDL3/SDL_events.h>
#include <string>
#include <vector>

struct SDL_Window;

class Application {
public:
    ~Application();

    bool init(const char* title, int width, int height);
    int exec(); // runs the main loop, returns exit code
    void openFile(const char* path) { m_userInterface.openFile(path); } // imported once running

private:
    // window
    SDL_Window* m_window = nullptr;
    bool m_running = false;
    void shutdown();

    // input / events
    InputCollector m_input; // SDL-based
    void processEvents();
    bool needsFrame() const;   // something changes on screen without new input
    void handleEvent(const SDL_Event& e);

    // internals
    GpuDevice m_gpu;
    Project m_project; // for canvas + graph
    
    // imgui
    UserInterface m_userInterface;
    std::vector<SDL_Event> m_imguiDeferredReleases;
    std::string m_iniPath; // layout file
    bool m_imguiReady = false;

    // render
    void applyContentScale();
    float m_contentScale = 0;
    void renderFrame();
    SystemCursors m_cursors;
    
    float m_acquireMs = 0, m_cpuMs = 0;

    // idle: no frames while nothing happens
    static constexpr int kIdleFrames = 6;   // frames rendered after the last event (ImGui settles over a few)
    int m_quietFrames = 0;                  // frames since the last event
};
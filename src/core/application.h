#pragma once
#include "core/input.h"
#include "core/system-cursors.h"
#include "core/project.h"
#include "gfx/gpu-context.h"
#include "ui/ui-main.h"

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

    // internals
    GpuContext m_gpu;
    Project m_project; // for canvas + graph    
    
    // imgui
    UserInterface m_userInterface;
    std::vector<SDL_Event> m_imguiDeferredReleases;
    std::string m_iniPath; // layout file
    bool m_imguiReady = false;

    // input / events
    InputCollector m_input; // SDL-based
    void processEvents();
    void handleEvent(const SDL_Event& e);

    // render
    void applyContentScale();
    float m_contentScale = 0;
    void renderFrame();
    SystemCursors m_cursors;
    
    float m_acquireMs = 0, m_cpuMs = 0;
};
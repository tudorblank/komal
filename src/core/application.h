#pragma once
#include "core/system-cursors.h"
#include "core/project.h"
#include "gfx/gpu-context.h"
#include "ui/ui-main.h"

#include <SDL3/SDL_events.h>
#include <vector>

struct SDL_Window;

class Application {
public:
    ~Application();

    bool init(const char* title, int width, int height);
    int  exec();                 // runs the main loop, returns exit code
    void openFile(const char* path) { m_userInterface.openFile(path); }   // imported once running

private:
    void processEvents();
    void handleEvent(const SDL_Event& e);
    void renderFrame();
    void applyContentScale();
    void shutdown();

    SDL_Window* m_window = nullptr;
    GpuContext  m_gpu;
    Project     m_project;   // before the window: panels hold references into it
    UserInterface m_userInterface;
    SystemCursors m_cursors;
    bool        m_running = false;
    bool        m_imguiReady = false;
    float       m_contentScale = 0;
    float       m_acquireMs = 0, m_cpuMs = 0;
    std::vector<SDL_Event> m_heldBack;   // releases held back to the next frame
};
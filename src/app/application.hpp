#pragma once
#include "app/input.hpp"
#include "app/window-surface.hpp"
#include "base/clock.hpp"
#include "base/frames.hpp"
#include "document/workspace.hpp"
#include "gfx/device.hpp"
#include "ui/ui-main.hpp"

#include <SDL3/SDL_events.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;

// runs the frames - draws on demand + installs the main loop's waker
class Application {
public:
    ~Application();

    // theme: a built-in one (assets/themes/<theme>.toml); main-dark if it can't be read
    bool init(const char* title, int width, int height, std::string_view theme = "main-dark");
    int  exec();
    void openFile(const char* path) { m_userInterface.openFile(path); } // imported once running

private:
    bool m_running = false;
    void shutdown();

    // window
    SDL_Window* m_window = nullptr;
    int m_normalW = 0, m_normalH = 0;   // non-maximized size
    std::string m_prefDir;  // settings
    std::string m_iniPath;  // imgui layout

    Workspace m_workspace;  // project + actions

    // input / events (from SDL)
    Input m_input;          // every event, to the UI's InputFrame and to ImGui (app/input.hpp)
    bool processEvents();
    void handleEvent(const SDL_Event& e);   // the app's own: quit, scale, size, drops

    // gpu
    WindowSurface m_winSurface; // surface helper
    GpuDevice m_gpu;
    bool initGpu();
    bool rebuildGpu();      // (device lost)
    int  m_gpuResetAt = 0;  // (KOMAL_GPU_RESET_AT=n) device lost at frame n (for rebuild)

    // ui
    UserInterface m_userInterface{ m_workspace.actions() };
    bool m_imguiReady = false;
    HostInfo m_host{ .uiScale = 0.0f, .gpu = {}, .showImportDialog = {} };   // what the interface reads about the window; scale 0: not styled yet
    void showImportDialog();
    void applyContentScale();
    
    // render
    void renderFrame();
    int  m_framesDrawn = 0;
    void initImGuiRenderer();
    bool m_imguiGpu = true;

    // frames - input changes can take a few frames to show
    static constexpr int kFramesAfterInput = 3;
    static constexpr Clock::Milliseconds kNoImageRetry{ 100.0 };   // between tries while the surface gives no image
    int m_framesAfterInput = kFramesAfterInput;     // still to render (the first frames too)
    Clock::TimePoint m_frameStart{};                // what nextFrameIn() counts from
    bool m_noImage = false;                         // the last frame got no swapchain image
    Clock::Seconds nextFrameIn() const;
    void waitForFrame();
};
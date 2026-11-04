#pragma once
#include "app/imgui-bridge.hpp"
#include "app/window-surface.hpp"
#include "base/clock.hpp"
#include "base/frames.hpp"
#include "document/documents.hpp"
#include "gpu/device.hpp"
#include "input/sdl-input.hpp"
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

    Documents m_documents;   // the open projects (each its workspace + actions); one active

    // input / events (from SDL)
    SdlInput    m_input;   // every event, into the InputFrame the UI reads (input/sdl-input.hpp)
    ImGuiBridge m_imgui;   // ... and into ImGui; ImGui's whole life (app/imgui-bridge.hpp)
    bool processEvents();
    void handleEvent(const SDL_Event& e);   // the app's own: quit, scale, size, drops

    // gpu
    WindowSurface m_winSurface; // surface helper
    GpuDevice m_gpu;
    bool initGpu();
    bool rebuildGpu();      // (device lost)
    int  m_gpuResetAt = 0;  // (KOMAL_GPU_RESET_AT=n) device lost at frame n (for rebuild)

    // ui
    UserInterface m_userInterface;   // a shell over the documents
    HostInfo m_host{ .uiScale = 0.0f, .gpu = {}, .showImportDialog = {}, .showOpenDialog = {}, .showSaveDialog = {} };   // what the interface reads about the window; scale 0: not styled yet
    void showImportDialog();
    void showOpenDialog();
    void showSaveDialog(DocumentId document, const std::string& suggested);
    void applyContentScale();
    
    // render
    void renderFrame();
    int  m_framesDrawn = 0;
    void logGpu();   // the adapter, driver and display, for the Info panel and the log

    // frames - input changes can take a few frames to show
    static constexpr int kFramesAfterInput = 3;
    static constexpr Clock::Milliseconds kNoImageRetry{ 100.0 };   // between tries while the surface gives no image
    int m_framesAfterInput = kFramesAfterInput;     // still to render (the first frames too)
    Clock::TimePoint m_frameStart{};                // what nextFrameIn() counts from
    bool m_noImage = false;                         // the last frame got no swapchain image
    Clock::Seconds nextFrameIn() const;
    void waitForFrame();
};
#include "application.h"
#include "core/perf.h"
#include "ui/theme.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <utility>
#include <vector>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>
#include <cstdio>

namespace {

// ImGui works in window coordinates and the SDL3 backend maps those to pixels itself
// (DisplayFramebufferScale), so sizes only need the remaining "content" part of the display
// scale: Xft.dpi / Windows scaling on X11 and Win32, ~1.0 on Wayland and macOS where the
// window is already measured in logical units. Using the full display scale there would
// apply HiDPI twice.
float contentScale(SDL_Window* window)
{
    const float display = SDL_GetWindowDisplayScale(window);
    const float density = SDL_GetWindowPixelDensity(window);
    return (display > 0.0f && density > 0.0f) ? display / density : 1.0f;
}

} // namespace

Application::~Application() { shutdown(); }

bool Application::init(const char* title, int width, int height)
{
#if defined(__linux__)
    // Prefer X11 (XWayland on KDE Wayland): NVIDIA + Vulkan swapchains pace much
    // better there. Falls back to Wayland if X11 is unavailable.
    // Override with: SDL_VIDEO_DRIVER=wayland ./Komal
    if (!SDL_getenv("SDL_VIDEO_DRIVER"))
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11,wayland");
#endif

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(__APPLE__)
    flags |= SDL_WINDOW_METAL;   // the wgpu surface is a CAMetalLayer (see surface-factory)
#endif
    m_window = SDL_CreateWindow(title, width, height, flags);
    if (!m_window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    if (!m_gpu.init(m_window))
        return false;

    // ---- Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // The layout lives in the per-user settings folder, wherever the app is started from (a
    // macOS app or a Windows shortcut doesn't start in a writable folder):
    //   Linux ~/.local/share/Komal/   Windows %APPDATA%\Komal\   macOS ~/Library/Application Support/Komal/
    if (char* dir = SDL_GetPrefPath(nullptr, "Komal")) {
        m_iniPath = std::string(dir) + "imgui.ini";
        SDL_free(dir);
    } else {
        m_iniPath = "imgui.ini";
    }
    io.IniFilename = m_iniPath.c_str();
    io.ConfigInputTrickleEventQueue = false;   // see processEvents()

    loadUiFonts();

    ImGui_ImplSDL3_InitForOther(m_window);
    m_cursors.init(m_window);

    ImGui_ImplWGPU_InitInfo wgpuInfo;
    wgpuInfo.Device             = m_gpu.device();
    wgpuInfo.NumFramesInFlight  = 3;
    wgpuInfo.RenderTargetFormat = m_gpu.surfaceFormat();
    wgpuInfo.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&wgpuInfo);

    {
        const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(m_window));
        char buf[512];
        std::snprintf(buf, sizeof buf,
                      "%s\nSDL video driver: %s | display: %.2f Hz | present: %s, latency %u | scale: %.2f x %.2f",
                      m_gpu.adapterInfo().c_str(), SDL_GetCurrentVideoDriver(),
                      dm ? dm->refresh_rate : 0.0f, m_gpu.presentModeName(), m_gpu.frameLatency(),
                      SDL_GetWindowPixelDensity(m_window), contentScale(m_window));
        m_userInterface.setGpuInfo(buf);
        std::fprintf(stderr, "[gpu] %s\n", buf);
    }
    m_imguiReady = true;
    m_project.init();
    m_userInterface.setWindow(m_window);
    if (!m_userInterface.init(m_gpu, m_project))
        std::fprintf(stderr, "UserInterface init failed (canvas viewport disabled)\n");
    applyContentScale();
    m_running = true;
    return true;
}

int Application::exec()
{
    while (m_running && !m_userInterface.quitRequested()) {
        if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) {
            SDL_WaitEventTimeout(nullptr, 100);   // sleep until something happens
            processEvents();
            continue;
        }
        renderFrame();
    }
    return 0;
}

void Application::processEvents()
{
    // ImGui's own input trickling is off (see init): with a fast mouse and a wheel or touchpad
    // scroll at once, it handled one alternation of event types per frame and the queue grew
    // without bound, so input lagged further and further behind. All events of a frame are
    // taken at once instead; only a release that arrives in the same batch as its press is
    // held back a frame, so a click or key tap shorter than a frame is never lost.
    std::vector<SDL_Event> events = std::exchange(m_heldBack, {});
    const size_t carried = events.size();
    SDL_Event e;
    while (SDL_PollEvent(&e)) events.push_back(e);

    uint32_t pressedButtons = 0;
    std::vector<SDL_Keycode> pressedKeys;
    for (size_t i = 0; i < events.size(); ++i) {
        const SDL_Event& ev = events[i];
        if (i >= carried) {
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) pressedButtons |= 1u << ev.button.button;
            if (ev.type == SDL_EVENT_KEY_DOWN) pressedKeys.push_back(ev.key.key);
            if ((ev.type == SDL_EVENT_MOUSE_BUTTON_UP && (pressedButtons & (1u << ev.button.button))) ||
                (ev.type == SDL_EVENT_KEY_UP && std::find(pressedKeys.begin(), pressedKeys.end(), ev.key.key) != pressedKeys.end())) {
                m_heldBack.push_back(ev);
                continue;
            }
        }
        handleEvent(ev);
    }
}

void Application::handleEvent(const SDL_Event& e)
{
    ImGui_ImplSDL3_ProcessEvent(&e);

    if (e.type == SDL_EVENT_MOUSE_MOTION)
        m_userInterface.addMouseSample(e.motion.x, e.motion.y);   // every sample, not just the frame's last
    if (e.type == SDL_EVENT_QUIT)
        m_running = false;
    if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
        e.window.windowID == SDL_GetWindowID(m_window))
        m_running = false;
    if (e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
        applyContentScale();
    if (e.type == SDL_EVENT_DROP_FILE && e.drop.data)
        m_userInterface.dropFile(e.drop.data, e.drop.x, e.drop.y);   // imported if it lands on the canvas or the graph
}

void Application::applyContentScale()
{
    const float scale = contentScale(m_window);
    if (scale == m_contentScale) return;
    m_contentScale = scale;
    applyUiStyle(scale);
    m_userInterface.setUiScale(scale);
}

void Application::renderFrame()
{
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);
    m_gpu.resize(w, h);

    const double freq = (double)SDL_GetPerformanceFrequency();
    const Uint64 t0 = SDL_GetPerformanceCounter();

    // Wait for the swapchain *before* reading input: with vsync this is where the frame
    // blocks, and every millisecond spent here would otherwise be added to input latency.
    Frame frame;
    const bool acquired = m_gpu.acquire(frame);
    const Uint64 t1 = SDL_GetPerformanceCounter();

    processEvents();
    if (!acquired)
        return;

    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    // Lay out for the image we actually hold: if the window was resized while we waited,
    // the new size is picked up next frame instead of overrunning this frame's target.
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)frame.width  / io.DisplayFramebufferScale.x,
                            (float)frame.height / io.DisplayFramebufferScale.y);
    ImGui::NewFrame();

    m_userInterface.setTimings(m_acquireMs, m_cpuMs);
    m_userInterface.draw();

    ImGui::Render();
    m_cursors.update();

    // offscreen passes first, in the same encoder: they finish before the surface pass samples them
    m_userInterface.renderGpu(frame.encoder);

    const float clear[4] = { 0.10f, 0.10f, 0.12f, 1.0f };
    m_gpu.beginSurfacePass(frame, clear);
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), frame.pass);
    m_gpu.present(frame);
    const Uint64 t2 = SDL_GetPerformanceCounter();

    // smoothed
    m_acquireMs = m_acquireMs * 0.95f + (float)((t1 - t0) * 1000.0 / freq) * 0.05f;
    m_cpuMs     = m_cpuMs     * 0.95f + (float)((t2 - t1) * 1000.0 / freq) * 0.05f;
    Perf::add(Perf::Frame, (t2 - t1) * 1000.0 / freq);

    Perf::endFrame();
}

void Application::shutdown()
{
    m_userInterface.shutdown();
    if (m_imguiReady) {
        m_cursors.shutdown();
        ImGui_ImplWGPU_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        m_imguiReady = false;
    }
    m_gpu.shutdown();
    if (m_window) { SDL_DestroyWindow(m_window); m_window = nullptr; }
    SDL_Quit();
}

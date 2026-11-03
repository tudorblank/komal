#include "application.hpp"
#include "app/imgui-frames.hpp"
#include "app/window-icon.hpp"
#include "app/window-state.hpp"
#include "base/dev-switches.hpp"
#include "base/log.hpp"
#include "base/perf.hpp"
#include "gfx/render-pass.hpp"
#include "ui/style/fonts.hpp"
#include "ui/style/ui-style.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>
#include <cstdio>
#include <string>

namespace {
    // main loop's waker (base/frames.hpp): an event wakes SDL_WaitEvent from any thread.
    void pushWakeEvent()
    {
        SDL_Event e{};
        e.type = SDL_EVENT_USER;
        SDL_PushEvent(&e);
    }

    float contentScale(SDL_Window* window)
    {
        const float display = SDL_GetWindowDisplayScale(window);
        const float density = SDL_GetWindowPixelDensity(window);
        return (display > 0.0f && density > 0.0f) ? display / density : 1.0f;
    }
}

Application::~Application() { shutdown(); }

bool Application::init(const char* title, int width, int height, std::string_view theme)
{
    // ---- the look's colours: before anything is drawn
    std::string why;
    if (!Style::useTheme(theme, why)) {
        Log::error("ui", "theme %s", why.c_str());
        if (!Style::useTheme("main-dark", why)) {
            Log::error("ui", "theme %s", why.c_str());
            return false;
        }
    }

    // ---- SDL (window)
#if defined(__linux__)
    // swapchains pace better on x11/xwayland; wayland if x11 unavailable
    if (!SDL_getenv("SDL_VIDEO_DRIVER"))
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11,wayland");
#endif

    // app id
    SDL_SetAppMetadata("Komal", nullptr, "komal");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        Log::error("sdl", "SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    installMainLoopWaker(&pushWakeEvent);

    // settings: ~/.local/share/Komal, %APPDATA%\Komal, ~/Library/Application Support/Komal.
    if (char* dir = SDL_GetPrefPath(nullptr, "Komal")) {
        m_prefDir = dir;
        SDL_free(dir);
    }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

#if defined(__APPLE__)
    flags |= SDL_WINDOW_METAL; // wgpu surface is a CAMetalLayer
#endif

    const WindowState saved = WindowState::load(m_prefDir + "window.cfg");
    if (saved.width > 0 && saved.height > 0) {
        SDL_Rect usable{};
        const bool known = SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
        width  = std::clamp(saved.width,  640, known ? std::max(640, usable.w) : saved.width);
        height = std::clamp(saved.height, 400, known ? std::max(400, usable.h) : saved.height);
        m_normalW = width;
        m_normalH = height;
        if (saved.maximized) {
            flags |= SDL_WINDOW_MAXIMIZED;
            if (known) { width = usable.w; height = usable.h; }
        }
    } else {
        m_normalW = width;
        m_normalH = height;
    }

    m_window = SDL_CreateWindow(title, width, height, flags);
    if (!m_window) {
        Log::error("sdl", "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    setWindowIcon(m_window);

    // ---- project (CPU)
    m_workspace.project().init();

    // ---- GPU init (wgpu)
    if (!initGpu()) return false;

    // ---- imgui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    m_iniPath = m_prefDir + "imgui.ini";   // the layout
    io.IniFilename = m_iniPath.c_str();
    io.ConfigInputTrickleEventQueue = false;   // ImGuiInputBridge does it, without the backlog

    Fonts::load();

    // ---- imgui on the GPU
    ImGui_ImplSDL3_InitForOther(m_window);
    initImGuiRenderer();
    m_imguiReady = true;

    // ---- user interface
    m_host.showImportDialog = [this] { showImportDialog(); };
    if (!m_userInterface.init(m_gpu, m_workspace.project(), m_input.frame(), m_host))
        Log::error("ui", "init failed (canvas viewport disabled)");
    applyContentScale();
    
    m_gpuResetAt = devSwitches().gpuResetAt;
    m_running = true;
    return true;
}

bool Application::initGpu()
{
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(m_window, &pw, &ph);
    return m_gpu.init([this](WGPUInstance instance) { return m_winSurface.create(instance, m_window); }, pw, ph);
}

void Application::initImGuiRenderer()
{
    ImGui_ImplWGPU_InitInfo wgpuInfo;
    wgpuInfo.Device             = m_gpu.device();
    wgpuInfo.NumFramesInFlight  = 3;
    wgpuInfo.RenderTargetFormat = m_gpu.surfaceFormat();
    wgpuInfo.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&wgpuInfo);

    const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(m_window));
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "%s\nSDL video driver: %s | display: %.2f Hz | present: %s, latency %u | scale: %.2f x %.2f",
                  m_gpu.adapterInfo().c_str(), SDL_GetCurrentVideoDriver(),
                  dm ? dm->refresh_rate : 0.0f, m_gpu.presentModeName(), m_gpu.frameLatency(),
                  SDL_GetWindowPixelDensity(m_window), contentScale(m_window));
    m_host.gpu = buf;
    Log::info("gpu", "%s", buf);
}

bool Application::rebuildGpu()
{
    Log::error("gpu", "the GPU device was lost: making everything on it again");
    if (m_imguiGpu) {   // what's on the old device goes, ImGui's too (its textures come back by themselves)
        m_userInterface.releaseGpu();
        ImGui_ImplWGPU_Shutdown();
        m_imguiGpu = false;
    }
    m_gpu.shutdown();
    m_winSurface.release();
    if (!initGpu()) {   // no device yet (a driver still resetting): tried again in a while
        m_gpu.shutdown();
        m_winSurface.release();
        return false;
    }
    initImGuiRenderer();
    m_imguiGpu = true;
    if (!m_userInterface.restoreGpu(m_gpu)) Log::error("ui", "the canvas couldn't be made on the new device");
    m_userInterface.notice("The GPU was reset: the picture is being drawn again");
    return true;
}

int Application::exec()
{
    while (m_running && !m_userInterface.quitRequested()) {
        if (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) {
            SDL_WaitEvent(nullptr);   // nothing to draw until it's back; leaves the event queued
            processEvents();
            continue;
        }
        waitForFrame();
        renderFrame();
    }
    return 0;
}

Clock::Seconds Application::nextFrameIn() const
{
    if (m_framesAfterInput > 0) return kFrameNow;
    return std::min(m_userInterface.nextFrameIn(), imguiNextFrameIn(m_input, m_frameStart));
}

void Application::waitForFrame()
{
    if (m_noImage) {   // no swapchain image last time (zero size, surface rebuilt): retry after input, else now and then
        if (m_framesAfterInput == 0) SDL_WaitEventTimeout(nullptr, Sint32(kNoImageRetry.count()));
        return;
    }
    const Clock::Seconds next = nextFrameIn();
    if (next <= kFrameNow) return;
    if (next == kNoFrame) {
        SDL_WaitEvent(nullptr);   // until input; leaves the event queued for processEvents()
        return;
    }
    const Clock::Milliseconds left = next - Clock::since(m_frameStart);
    if (left > kFrameNow) SDL_WaitEventTimeout(nullptr, Sint32(std::ceil(left.count())) + 1);   // just past it: no wake too early
}

bool Application::processEvents()
{
    m_input.beginFrame();
    bool any = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        any = true;
        m_input.handle(e);
        handleEvent(e);
    }
    m_input.endEvents();
    return any;
}

void Application::handleEvent(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_QUIT)
        m_running = false;
    if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
        e.window.windowID == SDL_GetWindowID(m_window))
        m_running = false;
    if (e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
        applyContentScale();
    if (e.type == SDL_EVENT_WINDOW_RESIZED &&
        !(SDL_GetWindowFlags(m_window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED))) {
        m_normalW = e.window.data1;   // the size to come back to un-maximized
        m_normalH = e.window.data2;
    }
    if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) {
        if (e.drop.windowID == 0)
            m_userInterface.openFile(e.drop.data);   // opened with the app (macOS Finder, Dock icon): no window, no position
        else
            m_userInterface.dropFile(e.drop.data, e.drop.x, e.drop.y);   // imported if it lands on the canvas or the graph
    }
}

void Application::showImportDialog()
{
    static const SDL_DialogFileFilter kFilters[] = {
        { "Images", "png;jpg;jpeg;bmp;tga;psd;gif" },
        { "All files", "*" },
    };
    // answers on its own thread: openFile() is safe from any
    SDL_ShowOpenFileDialog([](void* self, const char* const* files, int) {
        if (!files) return;   // an error, or cancelled
        for (; *files; ++files) static_cast<Application*>(self)->m_userInterface.openFile(*files);
    }, this, m_window, kFilters, 2, nullptr, true);
}

void Application::applyContentScale()
{
    const float scale = contentScale(m_window);
    if (scale == m_host.uiScale) return;
    m_host.uiScale = scale;
    Style::apply(scale);
}

void Application::renderFrame()
{
    if (m_gpuResetAt > 0 && ++m_framesDrawn == m_gpuResetAt) m_gpu.simulateLoss();
    if (m_gpu.lost() && !rebuildGpu()) {
        m_noImage = true;   // waitForFrame() spaces the retries out
        processEvents();
        return;
    }
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);
    m_gpu.resize(w, h);

    // wait for the swapchain BEFORE reading input
    const Clock::TimePoint t0 = Clock::now();
    GpuFrame frame;
    const bool acquired = m_gpu.acquire(frame);
    const Clock::TimePoint t1 = Clock::now();

    const bool input = processEvents();
    m_noImage = !acquired;
    if (!acquired) {   // a zero-size or rebuilt surface: the input waits for the retry
        m_framesAfterInput = input ? kFramesAfterInput + 1 : std::max(0, m_framesAfterInput - 1);
        return;
    }

    m_frameStart = Clock::now();
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(frame.width)  / io.DisplayFramebufferScale.x,
                            float(frame.height) / io.DisplayFramebufferScale.y);
    ImGui::NewFrame();

    m_userInterface.draw();

    ImGui::Render();

    // offscreen passes first, in the same encoder
    m_userInterface.renderGpu(frame.encoder);

    {
        const auto clear = Style::windowBackground().floats();
        const GpuRenderPass pass(frame.encoder, frame.view, GpuRenderPass::Load::Clear, "Surface", clear.data());
        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
    }
    m_gpu.present(frame);
    m_input.consumed();   // this frame's input was read

    // Frames still owed to this input (nextFrameIn()).
    m_framesAfterInput = input ? kFramesAfterInput : std::max(0, m_framesAfterInput - 1);

    Perf::add(Perf::Acquire, Clock::between(t0, t1));
    Perf::add(Perf::Frame, Clock::since(t1));
    Perf::endFrame();
}

void Application::shutdown()
{
    m_userInterface.shutdown();
    if (m_imguiReady) {
        if (m_imguiGpu) ImGui_ImplWGPU_Shutdown();
        m_imguiGpu = false;
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        m_imguiReady = false;
    }
    m_gpu.shutdown();
    m_winSurface.release();
    if (m_window) {
        if (!m_prefDir.empty())
            WindowState{ m_normalW, m_normalH, (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) != 0 }.save(m_prefDir + "window.cfg");
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}

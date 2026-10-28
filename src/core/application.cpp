#include "application.hpp"
#include "core/perf.hpp"
#include "ui/theme.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>
#include <cstdio>
#include <string>
#include <stb_image.h>

extern const unsigned char kAppIcon[];   // assets/komal-icon-256.png
extern const unsigned int  kAppIconSize;

namespace {
    // APP ICON //
    void setWindowIcon(SDL_Window* window)
    {
        int w = 0, h = 0, channels = 0;
        stbi_uc* px = stbi_load_from_memory(kAppIcon, int(kAppIconSize), &w, &h, &channels, 4);
        if (!px) return;
        if (SDL_Surface* icon = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, px, w * 4)) {
            SDL_SetWindowIcon(window, icon);   // copies the pixels
            SDL_DestroySurface(icon);
        }
        stbi_image_free(px);
    }

    // WINDOW STATE //
    struct WindowState { int width = 0, height = 0; bool maximized = false; };
    WindowState loadWindowState(const std::string& path)
    {
        WindowState s;
        if (FILE* f = std::fopen(path.c_str(), "r")) {
            int m = 0;
            if (std::fscanf(f, "%d %d %d", &s.width, &s.height, &m) == 3) s.maximized = m != 0;
            else s = {};
            std::fclose(f);
        }
        return s;
    }
    void saveWindowState(const std::string& path, const WindowState& s)
    {
        if (FILE* f = std::fopen(path.c_str(), "w")) {
            std::fprintf(f, "%d %d %d\n", s.width, s.height, s.maximized ? 1 : 0);
            std::fclose(f);
        }
    }

    // What ImGui's SDL3 backend reads (ImGui_ImplSDL3_ProcessEvent); the rest isn't queued for it.
bool forImGui(Uint32 type)
{
    switch (type) {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_DISPLAY_ORIENTATION:
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
    case SDL_EVENT_DISPLAY_MOVED:
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_MOUSE_ENTER:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
        return true;
    default:
        return false;
    }
}

// `b` continues the run of mouse moves `a` is in: only the last one matters.
bool sameRun(const SDL_Event& a, const SDL_Event& b)
{
    return a.motion.windowID == b.motion.windowID &&
           (a.motion.which == SDL_TOUCH_MOUSEID) == (b.motion.which == SDL_TOUCH_MOUSEID);   // ImGui tells touch apart
}

// When ImGui next changes what it draws by itself: a tooltip appearing while the mouse rests on
// an item, the caret blinking and keys repeating while typing. Ticks through those, from public
// state only. `sinceMove`: seconds from the last mouse move to this frame's start.
double imguiNextFrameIn(double sinceMove, bool keyHeld)
{
    constexpr double kTick        = 0.1;   // 10 frames a second
    constexpr double kTooltipRest = 0.6;   // every ImGui tooltip delay is shorter
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return keyHeld ? double(io.KeyRepeatRate) : kTick;
    if (ImGui::IsAnyItemHovered() && sinceMove < kTooltipRest) return kTick;
    return Application::kNoFrame;
}

// A key held that repeats while typing (not a modifier).
bool editingKeyHeld(const InputFrame& input)
{
    for (SDL_Keycode k : input.held)
        if (k != SDLK_LCTRL && k != SDLK_RCTRL && k != SDLK_LSHIFT && k != SDLK_RSHIFT &&
            k != SDLK_LALT && k != SDLK_RALT && k != SDLK_LGUI && k != SDLK_RGUI) return true;
    return false;
}

float contentScale(SDL_Window* window)
    {
        const float display = SDL_GetWindowDisplayScale(window);
        const float density = SDL_GetWindowPixelDensity(window);
        return (display > 0.0f && density > 0.0f) ? display / density : 1.0f;
    }
}

Application::~Application() { shutdown(); }

bool Application::init(const char* title, int width, int height)
{
    // ---- SDL (window)
#if defined(__linux__)
    // Prefer X11 (XWayland): swapchains pace better there. Wayland if X11 is unavailable.
    if (!SDL_getenv("SDL_VIDEO_DRIVER"))
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11,wayland");
#endif

    // The app id: desktops match the window to komal.desktop by it.
    SDL_SetAppMetadata("Komal", nullptr, "komal");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    // Settings: ~/.local/share/Komal, %APPDATA%\Komal, ~/Library/Application Support/Komal.
    if (char* dir = SDL_GetPrefPath(nullptr, "Komal")) {
        m_prefDir = dir;
        SDL_free(dir);
    }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

#if defined(__APPLE__)
    flags |= SDL_WINDOW_METAL; // wgpu surface is a CAMetalLayer
#endif

    // load window state
    const WindowState saved = loadWindowState(m_prefDir + "window.cfg");
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
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    setWindowIcon(m_window);

    // ---- project (CPU)
    m_project.init();

    // ---- GPU init (wgpu)
    if (!m_gpu.init(m_window))
        return false;

    // ---- UI init (ImGui)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    m_iniPath = m_prefDir + "imgui.ini";   // the layout
    io.IniFilename = m_iniPath.c_str();
    io.ConfigInputTrickleEventQueue = false;   // ImGuiFeed does it, without the backlog

    loadUiFonts();

    // ---- GPU setup
    ImGui_ImplSDL3_InitForOther(m_window);

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

    // ---- UI class pass
    m_userInterface.setWindow(m_window);
    if (!m_userInterface.init(m_gpu, m_project, m_input.frame()))
        std::fprintf(stderr, "UserInterface init failed (canvas viewport disabled)\n");
    applyContentScale();
    
    m_running = true;
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

double Application::nextFrameIn() const
{
    if (m_framesAfterInput > 0 || m_imguiFeed.holding()) return 0.0;
    const double sinceMove = m_lastMoveNs < m_frameStartNs ? double(m_frameStartNs - m_lastMoveNs) * 1e-9 : 0.0;
    return std::min(m_userInterface.nextFrameIn(), imguiNextFrameIn(sinceMove, editingKeyHeld(m_input.frame())));
}

void Application::waitForFrame()
{
    if (m_noImage) {   // no swapchain image last time (zero size, surface rebuilt): retry after input, else now and then
        if (m_framesAfterInput == 0) SDL_WaitEventTimeout(nullptr, kNoImageRetryMs);
        return;
    }
    const double next = nextFrameIn();
    if (next <= 0.0) return;
    if (next == kNoFrame) {
        SDL_WaitEvent(nullptr);   // until input; leaves the event queued for processEvents()
        return;
    }
    const double left = next - double(SDL_GetTicksNS() - m_frameStartNs) * 1e-9;
    if (left > 0.0) SDL_WaitEventTimeout(nullptr, Sint32(std::ceil(left * 1000.0)) + 1);   // just past it: no wake too early
}

bool Application::processEvents()
{
    m_input.beginFrame();
    m_imguiFeed.beginFrame();
    bool any = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        any = true;
        if (e.type == SDL_EVENT_MOUSE_MOTION) m_lastMoveNs = e.common.timestamp;
        m_input.handle(e);
        m_imguiFeed.push(e);
        handleEvent(e);
    }
    m_imguiFeed.endEvents();
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

    // wait for the swapchain BEFORE reading input
    GpuFrame frame;
    const bool acquired = m_gpu.acquire(frame);
    const Uint64 t1 = SDL_GetPerformanceCounter();

    const bool input = processEvents();
    m_noImage = !acquired;
    if (!acquired) {   // a zero-size or rebuilt surface: the input waits for the retry
        m_framesAfterInput = input ? kFramesAfterInput + 1 : std::max(0, m_framesAfterInput - 1);
        return;
    }

    m_frameStartNs = SDL_GetTicksNS();
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    
    // layout
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)frame.width  / io.DisplayFramebufferScale.x,
                            (float)frame.height / io.DisplayFramebufferScale.y);
    ImGui::NewFrame();

    m_userInterface.setTimings(m_acquireMs, m_cpuMs);
    m_userInterface.draw();

    ImGui::Render();

    // offscreen passes first, in the same encoder
    m_userInterface.renderGpu(frame.encoder);

    const float clear[4] = { 0.055f, 0.055f, 0.055f, 1.0f };
    m_gpu.beginSurfacePass(frame, clear);
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), frame.pass);
    m_gpu.present(frame);
    const Uint64 t2 = SDL_GetPerformanceCounter();

    // Frames still owed to this input (nextFrameIn()).
    m_framesAfterInput = input ? kFramesAfterInput : std::max(0, m_framesAfterInput - 1);

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
        ImGui_ImplWGPU_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        m_imguiReady = false;
    }
    m_gpu.shutdown();
    if (m_window) {
        if (!m_prefDir.empty())
            saveWindowState(m_prefDir + "window.cfg", { m_normalW, m_normalH, (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) != 0 });
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}

// ---------------------------------------------------------------- frames, from anywhere

void Application::wakeMainLoop()
{
    SDL_Event e{};
    e.type = SDL_EVENT_USER;
    SDL_PushEvent(&e);
}

// ---------------------------------------------------------------- ImGui's input

void Application::ImGuiFeed::beginFrame()
{
    m_buttonsChanged = 0;
    m_keysChanged.clear();
    m_cut = false;
    std::swap(m_held, m_replay);
    const bool flood = m_replay.size() > kMaxHeld;
    for (Held& h : m_replay) {
        if (h.event.type == SDL_EVENT_TEXT_INPUT) h.event.text.text = h.text.c_str();
        if (flood) ImGui_ImplSDL3_ProcessEvent(&h.event);
        else       push(h.event);
    }
    m_replay.clear();
}

void Application::ImGuiFeed::push(const SDL_Event& e)
{
    if (!forImGui(e.type)) return;
    if (m_cut) { hold(e); return; }
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
        if (m_hasMove && !sameRun(m_move, e)) flushMove();
        m_move    = e;
        m_hasMove = true;
        return;
    }
    flushMove();   // where the mouse was when this happened
    if (changedTwice(e)) {
        m_cut = true;
        hold(e);
        return;
    }
    ImGui_ImplSDL3_ProcessEvent(&e);
}

void Application::ImGuiFeed::endEvents()
{
    flushMove();
}

void Application::ImGuiFeed::flushMove()
{
    if (!m_hasMove) return;
    ImGui_ImplSDL3_ProcessEvent(&m_move);
    m_hasMove = false;
}

bool Application::ImGuiFeed::changedTwice(const SDL_Event& e)
{
    switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        const uint32_t bit = 1u << (e.button.button & 31u);
        if (m_buttonsChanged & bit) return true;
        m_buttonsChanged |= bit;
        return false;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (e.key.repeat) return false;   // still down: no change
        if (std::find(m_keysChanged.begin(), m_keysChanged.end(), e.key.scancode) != m_keysChanged.end()) return true;
        m_keysChanged.push_back(e.key.scancode);
        return false;
    default:
        return false;
    }
}

void Application::ImGuiFeed::hold(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_MOUSE_MOTION && !m_held.empty() && m_held.back().event.type == SDL_EVENT_MOUSE_MOTION &&
        sameRun(m_held.back().event, e)) {
        m_held.back().event = e;
        return;
    }
    Held& h = m_held.emplace_back();
    h.event = e;
    h.text.clear();
    if (e.type == SDL_EVENT_TEXT_INPUT && e.text.text) h.text = e.text.text;
}

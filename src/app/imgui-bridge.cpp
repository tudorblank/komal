#include "app/imgui-bridge.hpp"
#include "base/frames.hpp"
#include "gfx/device.hpp"
#include "input/input-frame.hpp"
#include "ui/style/fonts.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>
#include <algorithm>
#include <utility>

namespace {

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

// A key held that repeats while typing (not a modifier).
bool editingKeyHeld(const InputFrame& input)
{
    for (Key k : input.held)
        if (!isModifierKey(k)) return true;
    return false;
}

}


// ---------------------------------------------------------------- lifetime

void ImGuiBridge::init(SDL_Window* window, const GpuDevice& gpu, std::string iniPath)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    m_iniPath = std::move(iniPath);
    io.IniFilename = m_iniPath.c_str();          // the layout
    io.ConfigInputTrickleEventQueue = false;     // the pacing below does it, without the backlog

    Fonts::load();
    ImGui_ImplSDL3_InitForOther(window);
    initRenderer(gpu);
    m_ready = true;
}

void ImGuiBridge::shutdown()
{
    if (!m_ready) return;
    releaseGpu();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    m_ready = false;
}

void ImGuiBridge::initRenderer(const GpuDevice& gpu)
{
    ImGui_ImplWGPU_InitInfo info;
    info.Device             = gpu.device();
    info.NumFramesInFlight  = 3;
    info.RenderTargetFormat = gpu.surfaceFormat();
    info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&info);
    m_renderer = true;
}

void ImGuiBridge::releaseGpu()
{
    if (!m_renderer) return;
    ImGui_ImplWGPU_Shutdown();   // its textures come back by themselves on the new device
    m_renderer = false;
}

void ImGuiBridge::restoreGpu(const GpuDevice& gpu)
{
    if (m_ready && !m_renderer) initRenderer(gpu);
}

// ---------------------------------------------------------------- frames

void ImGuiBridge::newFrame(uint32_t pixelWidth, uint32_t pixelHeight)
{
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(pixelWidth)  / io.DisplayFramebufferScale.x,
                            float(pixelHeight) / io.DisplayFramebufferScale.y);
    ImGui::NewFrame();
}

void ImGuiBridge::endFrame()
{
    ImGui::Render();
}

void ImGuiBridge::draw(WGPURenderPassEncoder pass)
{
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
}

Clock::Seconds ImGuiBridge::nextFrameIn(const InputFrame& input, Clock::TimePoint frameStart) const
{
    constexpr Clock::Seconds kTick{ 0.1 };          // 10 frames a second
    constexpr Clock::Seconds kTooltipRest{ 0.6 };   // every ImGui tooltip delay is shorter
    if (!m_held.empty()) return kFrameNow;          // events wait for the next frame
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return editingKeyHeld(input) ? Clock::Seconds(double(io.KeyRepeatRate)) : kTick;
    const Clock::Seconds sinceMove = std::max(Clock::Seconds(0), Clock::between(m_lastMove, frameStart));
    if (ImGui::IsAnyItemHovered() && sinceMove < kTooltipRest) return kTick;
    return kNoFrame;
}

// ---------------------------------------------------------------- events

void ImGuiBridge::beginEvents()
{
    m_buttonsChanged = 0;
    m_keysChanged.clear();
    m_cut = false;
    std::swap(m_held, m_replay);
    const bool flood = m_replay.size() > kMaxHeld;
    for (Held& h : m_replay) {
        if (h.event.type == SDL_EVENT_TEXT_INPUT) h.event.text.text = h.text.c_str();
        if (flood) ImGui_ImplSDL3_ProcessEvent(&h.event);
        else       handle(h.event);
    }
    m_replay.clear();
}

void ImGuiBridge::handle(const SDL_Event& e)
{
    if (!forImGui(e.type)) return;
    if (e.type == SDL_EVENT_MOUSE_MOTION) m_lastMove = Clock::now();
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

void ImGuiBridge::endEvents()
{
    flushMove();
}

void ImGuiBridge::flushMove()
{
    if (!m_hasMove) return;
    ImGui_ImplSDL3_ProcessEvent(&m_move);
    m_hasMove = false;
}

bool ImGuiBridge::changedTwice(const SDL_Event& e)
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

void ImGuiBridge::hold(const SDL_Event& e)
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

#include "app/imgui-feed.hpp"
#include "base/frames.hpp"
#include "command/input.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <algorithm>

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
    for (SDL_Keycode k : input.held)
        if (k != SDLK_LCTRL && k != SDLK_RCTRL && k != SDLK_LSHIFT && k != SDLK_RSHIFT &&
            k != SDLK_LALT && k != SDLK_RALT && k != SDLK_LGUI && k != SDLK_RGUI) return true;
    return false;
}

}

void ImGuiFeed::beginFrame()
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

void ImGuiFeed::push(const SDL_Event& e)
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

void ImGuiFeed::endEvents()
{
    flushMove();
}

double ImGuiFeed::nextFrameIn(Clock::TimePoint frameStart, const InputFrame& input) const
{
    constexpr double kTick        = 0.1;   // 10 frames a second
    constexpr double kTooltipRest = 0.6;   // every ImGui tooltip delay is shorter
    if (!m_held.empty()) return 0.0;       // events wait for the next frame
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return editingKeyHeld(input) ? double(io.KeyRepeatRate) : kTick;
    const double sinceMove = std::max(0.0, Clock::msBetween(m_lastMove, frameStart) / 1000.0);
    if (ImGui::IsAnyItemHovered() && sinceMove < kTooltipRest) return kTick;
    return kNoFrame;
}

void ImGuiFeed::flushMove()
{
    if (!m_hasMove) return;
    ImGui_ImplSDL3_ProcessEvent(&m_move);
    m_hasMove = false;
}

bool ImGuiFeed::changedTwice(const SDL_Event& e)
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

void ImGuiFeed::hold(const SDL_Event& e)
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

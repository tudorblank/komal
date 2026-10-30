#include "input.hpp"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pen.h>
#include <algorithm>

namespace {

bool fromPen(SDL_MouseID which) { return which == SDL_PEN_MOUSEID; }   // a pen also reported as a mouse

uint8_t keyMods(SDL_Keymod m)
{
    return uint8_t((m & SDL_KMOD_CTRL  ? ModCtrl  : 0) | (m & SDL_KMOD_SHIFT ? ModShift : 0) |
                   (m & SDL_KMOD_ALT   ? ModAlt   : 0) | (m & SDL_KMOD_GUI   ? ModSuper : 0));
}

bool mouseButton(Uint8 sdlButton, PointerButton& out)
{
    switch (sdlButton) {
    case SDL_BUTTON_LEFT:   out = PointerButton::Primary;   return true;
    case SDL_BUTTON_RIGHT:  out = PointerButton::Secondary; return true;
    case SDL_BUTTON_MIDDLE: out = PointerButton::Middle;    return true;
    default:                return false;   // X1 / X2: not pointer buttons for tools
    }
}

}

bool InputFrame::pressed(Shortcut s, bool repeats) const
{
    for (const KeyEvent& k : keys)
        if (k.down && k.key == s.key && k.mods == s.mods && (repeats || !k.repeat)) return true;
    return false;
}

bool InputFrame::isDown(SDL_Keycode key) const
{
    return std::find(held.begin(), held.end(), key) != held.end();
}

void InputCollector::beginFrame()
{
    m_frame.pointer.clear();
    m_frame.keys.clear();
}

void InputCollector::releaseAll()
{
    m_frame.held.clear();
    m_frame.mods = ModNone;
    m_frame.mouseButtons = 0;
    m_frame.penButtons   = 0;
    m_frame.pen.touching = false;
}

void InputCollector::push(PointerEvent ev)
{
    if (ev.type != PointerEvent::Type::Move) {
        uint8_t& held = ev.device == PointerDevice::Mouse ? m_frame.mouseButtons : m_frame.penButtons;
        const uint8_t bit = uint8_t(1u << uint8_t(ev.button));
        held = ev.type == PointerEvent::Type::Down ? uint8_t(held | bit) : uint8_t(held & ~bit);
    }
    // pen report (multiple events, folded)
    if (ev.type == PointerEvent::Type::Move && !m_frame.pointer.empty()) {
        PointerEvent& last = m_frame.pointer.back();
        if (last.type == PointerEvent::Type::Move && last.device == ev.device && last.timeNs == ev.timeNs &&
            last.x == ev.x && last.y == ev.y) {
            last = ev;
            return;
        }
    }
    m_frame.lastDevice = ev.device;
    m_frame.pointer.push_back(ev);
}

PointerEvent InputCollector::penEvent(PointerEvent::Type type, float x, float y, uint64_t timeNs) const
{
    PointerEvent ev;
    ev.type     = type;
    ev.device   = m_frame.pen.eraser ? PointerDevice::Eraser : PointerDevice::Pen;
    ev.x        = x;
    ev.y        = y;
    ev.pressure = m_frame.pen.pressure;
    ev.tiltX    = m_frame.pen.tiltX;
    ev.tiltY    = m_frame.pen.tiltY;
    ev.timeNs   = timeNs;
    return ev;
}

void InputCollector::handle(const SDL_Event& e)
{
    InputFrame::Pen& pen = m_frame.pen;
    switch (e.type) {
    // ---- keyboard ---- //
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        KeyEvent k;
        k.key      = e.key.key;
        k.scancode = e.key.scancode;
        k.mods     = keyMods(e.key.mod);
        k.down     = e.key.down;
        k.repeat   = e.key.repeat;
        k.timeNs   = e.key.timestamp;
        m_frame.keys.push_back(k);
        // From SDL's keyboard state: an event's `mod` can predate its own key (Ctrl alone).
        m_frame.mods = keyMods(SDL_GetModState());
        if (k.down && !m_frame.isDown(k.key)) m_frame.held.push_back(k.key);
        if (!k.down) std::erase(m_frame.held, k.key);
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:   // keys and buttons released elsewhere never come back up here
        releaseAll();
        break;

    // ---- mouse ---- //
    case SDL_EVENT_MOUSE_MOTION: {
        if (fromPen(e.motion.which)) break;
        PointerEvent ev;
        ev.x = e.motion.x;
        ev.y = e.motion.y;
        ev.timeNs = e.motion.timestamp;
        push(ev);
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        PointerEvent ev;
        if (fromPen(e.button.which) || !mouseButton(e.button.button, ev.button)) break;
        ev.type   = e.button.down ? PointerEvent::Type::Down : PointerEvent::Type::Up;
        ev.x      = e.button.x;
        ev.y      = e.button.y;
        ev.timeNs = e.button.timestamp;
        push(ev);
        break;
    }
    // ---- pen ---- //
    case SDL_EVENT_PEN_PROXIMITY_IN:
        pen.present = true;
        break;
    case SDL_EVENT_PEN_PROXIMITY_OUT:
        pen.touching = false;
        pen.pressure = 0.0f;
        m_frame.penButtons = 0;
        break;
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP:
        pen.present  = true;
        pen.eraser   = e.ptouch.eraser;
        pen.touching = e.ptouch.down;
        if (!e.ptouch.down) pen.pressure = 0.0f;
        push(penEvent(e.ptouch.down ? PointerEvent::Type::Down : PointerEvent::Type::Up, e.ptouch.x, e.ptouch.y, e.ptouch.timestamp));
        break;
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP: {
        if (e.pbutton.button < 1 || e.pbutton.button > 2) break;   // barrel buttons 1 and 2
        PointerEvent ev = penEvent(e.pbutton.down ? PointerEvent::Type::Down : PointerEvent::Type::Up,
                                   e.pbutton.x, e.pbutton.y, e.pbutton.timestamp);
        ev.button = e.pbutton.button == 1 ? PointerButton::Secondary : PointerButton::Middle;
        push(ev);
        break;
    }
    case SDL_EVENT_PEN_MOTION:
        pen.present = true;
        pen.eraser  = (e.pmotion.pen_state & SDL_PEN_INPUT_ERASER_TIP) != 0;
        push(penEvent(PointerEvent::Type::Move, e.pmotion.x, e.pmotion.y, e.pmotion.timestamp));
        break;
    case SDL_EVENT_PEN_AXIS:
        switch (e.paxis.axis) {
        case SDL_PEN_AXIS_PRESSURE: pen.pressure = std::clamp(e.paxis.value, 0.0f, 1.0f); break;
        case SDL_PEN_AXIS_XTILT:    pen.tiltX = e.paxis.value; break;
        case SDL_PEN_AXIS_YTILT:    pen.tiltY = e.paxis.value; break;
        default:                    return;   // distance, rotation, ...: not used yet
        }
        // pressing harder
        push(penEvent(PointerEvent::Type::Move, e.paxis.x, e.paxis.y, e.paxis.timestamp));
        break;
    default:
        break;
    }
}
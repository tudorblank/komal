#include "input/pointer-capture.hpp"

PointerCapture::Step PointerCapture::feed(const PointerEvent& ev, bool mayBegin)
{
    using Type = PointerEvent::Type;
    if (!m_held) {
        if (ev.type != Type::Down || !mayBegin) return Step::Ignored;
        m_held   = true;
        m_pen    = isPen(ev.device);
        m_button = ev.button;
        return Step::Began;
    }
    if (isPen(ev.device) != m_pen) return Step::Ignored;   // the other device
    switch (ev.type) {
    case Type::Move: return Step::Moved;
    case Type::Up:
        if (ev.button != m_button) return Step::Ignored;
        m_held = false;
        return Step::Ended;
    case Type::Down: return Step::Ignored;   // another button, while this one's held
    }
    return Step::Ignored;
}

bool PointerCapture::lost(const InputFrame& frame)
{
    if (!m_held || frame.buttonHeld(m_pen ? PointerDevice::Pen : PointerDevice::Mouse, m_button)) return false;
    m_held = false;
    return true;
}

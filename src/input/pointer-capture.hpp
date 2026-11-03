#pragma once
// One press followed to its release: what the canvas tools stroke and drag with. It follows the
// device that pressed (the mouse, or the pen: tip and eraser alike), so the mouse nudged
// mid-stroke never joins the pen's; it ends when that button comes up, or when the frame shows
// it isn't held any more (a release the app never saw: focus lost mid-drag).
//
//   switch (m_capture.feed(ev, mayBegin)) {
//   case PointerCapture::Step::Began: ...   // ev pressed it
//   case PointerCapture::Step::Moved: ...   // the holder moved
//   case PointerCapture::Step::Ended: ...   // the holder let go: ev is the release
//   case PointerCapture::Step::Ignored: break;
//   }
//   if (m_capture.lost(frame)) ...           // ended without a release

#include "input/input-frame.hpp"

#include <cstdint>

class PointerCapture {
public:
    enum class Step : uint8_t { Ignored, Began, Moved, Ended };

    // What `ev` does to it. A press begins one only while nothing is held and `mayBegin` (the
    // button, and where it landed, are the caller's to judge).
    Step feed(const PointerEvent& ev, bool mayBegin);
    // Its button isn't held any more, yet no release came: ends it. True if it did.
    bool lost(const InputFrame& frame);
    void release() { m_held = false; }   // ended from outside (the tool switched away)

    bool held() const { return m_held; }
    PointerButton button() const { return m_button; }
    bool pen() const { return m_pen; }   // held by the pen (tip or eraser), not the mouse

private:
    static bool isPen(PointerDevice d) { return d != PointerDevice::Mouse; }

    bool          m_held   = false;
    bool          m_pen    = false;
    PointerButton m_button = PointerButton::Primary;
};

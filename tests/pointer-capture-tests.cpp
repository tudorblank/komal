// A press followed to its release: by its device and button only, ended by a lost release too.

#include "input/pointer-capture.hpp"

#include <doctest/doctest.h>

namespace {

using Step = PointerCapture::Step;
using Type = PointerEvent::Type;

PointerEvent event(Type type, PointerDevice device = PointerDevice::Mouse, PointerButton button = PointerButton::Primary)
{
    PointerEvent ev;
    ev.type   = type;
    ev.device = device;
    ev.button = button;
    return ev;
}

}

TEST_CASE("a capture follows the press that began it, to its release")
{
    PointerCapture c;
    CHECK(c.feed(event(Type::Move), true) == Step::Ignored);           // nothing held: moves pass by
    CHECK(c.feed(event(Type::Down), false) == Step::Ignored);          // the caller said no
    CHECK_FALSE(c.held());
    REQUIRE(c.feed(event(Type::Down, PointerDevice::Pen), true) == Step::Began);
    CHECK(c.pen());
    CHECK(c.feed(event(Type::Move, PointerDevice::Mouse), true) == Step::Ignored);   // the other device
    CHECK(c.feed(event(Type::Move, PointerDevice::Eraser), true) == Step::Moved);    // the pen's other end: the pen
    CHECK(c.feed(event(Type::Down, PointerDevice::Pen, PointerButton::Secondary), true) == Step::Ignored);
    CHECK(c.feed(event(Type::Up, PointerDevice::Pen, PointerButton::Secondary), true) == Step::Ignored);   // not its button
    CHECK(c.feed(event(Type::Up, PointerDevice::Mouse), true) == Step::Ignored);
    CHECK(c.held());
    CHECK(c.feed(event(Type::Up, PointerDevice::Pen), true) == Step::Ended);
    CHECK_FALSE(c.held());
    CHECK(c.feed(event(Type::Move, PointerDevice::Pen), true) == Step::Ignored);
}

TEST_CASE("a capture whose release never came ends once its button isn't held")
{
    PointerCapture c;
    REQUIRE(c.feed(event(Type::Down, PointerDevice::Mouse, PointerButton::Secondary), true) == Step::Began);
    InputFrame frame;
    frame.mouseButtons = 1u << unsigned(PointerButton::Secondary);
    CHECK_FALSE(c.lost(frame));   // still held
    frame.mouseButtons = 1u << unsigned(PointerButton::Primary);
    CHECK(c.lost(frame));         // another button is, but not this one
    CHECK_FALSE(c.held());
    CHECK_FALSE(c.lost(frame));   // once
}

TEST_CASE("a capture let go from outside ignores the rest of its press")
{
    PointerCapture c;
    REQUIRE(c.feed(event(Type::Down), true) == Step::Began);
    c.release();
    CHECK(c.feed(event(Type::Move), true) == Step::Ignored);
    CHECK(c.feed(event(Type::Up), true) == Step::Ignored);
}

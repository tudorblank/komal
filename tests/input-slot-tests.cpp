#include "document/input-slot.hpp"

#include <doctest/doctest.h>

TEST_CASE("an input slot round-trips through its text")
{
    const InputSlot slots[] = { InputSlot::input(), InputSlot::stackLayer(2), InputSlot::newLayer(0),
                                InputSlot::mask(NodeSignal::Wet, NodeChannel::G) };
    for (const InputSlot& s : slots) CHECK(InputSlot::parse(s.text()) == s);
    CHECK(InputSlot::stackLayer(2).text() == "layer:2");
    CHECK(InputSlot::mask(NodeSignal::Main, NodeChannel::A).text() == "mask:main.a");
    CHECK(InputSlot::stackLayer(1) != InputSlot::newLayer(1));   // same number, different places
}

TEST_CASE("text that isn't a slot is refused")
{
    for (const char* bad : { "", "inputs", "layer:", "layer:x", "layer:-1", "new:2a", "mask:main", "mask:main.x", "mask:blur.a", "mask:.a" })
        CHECK_FALSE(InputSlot::parse(bad));
    CHECK(InputSlot::valid("mask:dry.b"));
}

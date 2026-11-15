#include "base/color.hpp"

#include <doctest/doctest.h>

TEST_CASE("hexDigit(): either case, -1 for anything else")
{
    static_assert(hexDigit('0') == 0 && hexDigit('9') == 9);
    static_assert(hexDigit('a') == 10 && hexDigit('F') == 15);
    for (const char bad : { 'g', 'G', ' ', '#', '/', ':', '@', '`' }) {
        CAPTURE(bad);
        CHECK(hexDigit(bad) == -1);
    }
}

TEST_CASE("colors as text")
{
    CHECK(Color8{ 1, 171, 255, 0 }.text() == "#01ABFF00");
    CHECK(Color8::parse("#01abff00") == Color8{ 1, 171, 255, 0 });
    for (const char* bad : { "", "#", "01ABFF00", "#01ABFF0", "#01ABFF000", "#01ABFG00", "#01 BFF00" }) {
        CAPTURE(bad);
        CHECK_FALSE(Color8::parse(bad));
    }
}

TEST_CASE("hex(): a color worked out while compiling, opaque")
{
    static_assert(hex("#01ABFF") == Color8{ 1, 171, 255, 255 });
    static_assert(hex("#01abff") == hex("#01ABFF"));   // either case
    static_assert(hex("#000000") == kPureBlack && hex("#FFFFFF") == kPureWhite);
    // A typo (hex("#01ABFG"), hex("01ABFF0")) doesn't compile: there's nothing to check at runtime.
}

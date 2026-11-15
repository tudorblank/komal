// The GPU's tests: the canvas shaders compiled and run on a device with no window, held to the
// CPU's maths. Built and run with every build (CMakeLists.txt: KOMAL_TESTS); with no GPU to run
// on they're skipped, not failed.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

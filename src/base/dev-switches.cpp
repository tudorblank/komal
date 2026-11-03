#include "base/dev-switches.hpp"

#include <cstdlib>

namespace {

    DevSwitches read()
    {
        DevSwitches s;
        s.perfLog = std::getenv("KOMAL_PERF") != nullptr;
        if (const char* v = std::getenv("KOMAL_PRESENT_MODE")) s.presentMode = v;
        if (const char* v = std::getenv("KOMAL_FRAME_LATENCY")) s.frameLatency = std::atoi(v);
        if (const char* v = std::getenv("KOMAL_GPU_RESET_AT")) s.gpuResetAt = std::atoi(v);
        return s;
    }

}

const DevSwitches& devSwitches()
{
    static const DevSwitches switches = read();
    return switches;
}
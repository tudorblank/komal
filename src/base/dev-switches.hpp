#pragma once
// The environment variables the app reads, all here: developer switches, never the user's settings.
//
//   KOMAL_PERF=1                 the frame timings printed every second (base/perf)
//   KOMAL_PRESENT_MODE=mode      fifo (default), fifo_relaxed, mailbox, immediate (gfx/device)
//   KOMAL_FRAME_LATENCY=n        swapchain images queued ahead of the display, 1..3 (gfx/device)
//   KOMAL_GPU_RESET_AT=n         the GPU device is "lost" at frame n: tries the rebuild (app)

#include <string>

struct DevSwitches {
    bool        perfLog = false;
    std::string presentMode;      // empty: the default
    int         frameLatency = 0; // 0: the default
    int         gpuResetAt = 0;   // 0: never
};

const DevSwitches& devSwitches();   // read once, at first use
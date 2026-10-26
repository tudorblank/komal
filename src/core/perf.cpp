#include "perf.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace Perf {
namespace {

struct State {
    double frame[Count] = {};     // this frame so far
    double smooth[Count] = {};
    double worstNow[Count] = {}, worstShown[Count] = {};
    double sum[Count] = {};
    int    frames = 0;
    std::chrono::steady_clock::time_point windowStart = std::chrono::steady_clock::now();
    bool   print = std::getenv("KOMAL_PERF") != nullptr;
};
State g;

constexpr const char* kNames[Count] = { "frame", "stroke", "canvas", "canvas sync", "thumbnails", "graph" };

}

void add(Counter c, double ms) { g.frame[c] += ms; }

void endFrame()
{
    for (int c = 0; c < Count; ++c) {
        g.smooth[c]   = g.smooth[c] * 0.9 + g.frame[c] * 0.1;
        g.worstNow[c] = std::max(g.worstNow[c], g.frame[c]);
        g.sum[c]     += g.frame[c];
        g.frame[c]    = 0.0;
    }
    ++g.frames;
    const auto now = std::chrono::steady_clock::now();
    if (now - g.windowStart < std::chrono::seconds(1)) return;
    if (g.print) {
        std::fprintf(stderr, "[perf] %3d fps |", g.frames);
        for (int c = 0; c < Count; ++c) std::fprintf(stderr, " %s %.2f/%.2f", kNames[c], g.sum[c] / g.frames, g.worstNow[c]);
        std::fprintf(stderr, "\n");
    }
    for (int c = 0; c < Count; ++c) { g.worstShown[c] = g.worstNow[c]; g.worstNow[c] = 0.0; g.sum[c] = 0.0; }
    g.frames = 0;
    g.windowStart = now;
}

double average(Counter c) { return g.smooth[c]; }
double worst(Counter c)   { return g.worstShown[c]; }
const char* name(Counter c) { return kNames[c]; }

}

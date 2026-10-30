#include "base/perf.hpp"
#include "base/log.hpp"

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
    int    frames = 0, framesShown = 0;
    Clock::TimePoint windowStart = Clock::now();
    bool   print = std::getenv("KOMAL_PERF") != nullptr;
};
State g;

constexpr const char* kNames[Count] = { "frame", "acquire", "stroke", "canvas", "canvas sync", "thumbnails", "graph" };

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
    const Clock::TimePoint now = Clock::now();
    if (Clock::msBetween(g.windowStart, now) < 1000.0) return;
    if (g.print) {   // one line: fps, then each counter's average/worst ms
        char line[512];
        int n = std::snprintf(line, sizeof line, "%3d fps |", g.frames);
        for (int c = 0; c < Count && n > 0 && n < int(sizeof line); ++c)
            n += std::snprintf(line + n, sizeof line - size_t(n), " %s %.2f/%.2f", kNames[c], g.sum[c] / g.frames, g.worstNow[c]);
        Log::info("perf", "%s", line);
    }
    for (int c = 0; c < Count; ++c) { g.worstShown[c] = g.worstNow[c]; g.worstNow[c] = 0.0; g.sum[c] = 0.0; }
    g.framesShown = g.frames;
    g.frames = 0;
    g.windowStart = now;
}

double average(Counter c) { return g.smooth[c]; }
double worst(Counter c)   { return g.worstShown[c]; }
const char* name(Counter c) { return kNames[c]; }
int fps() { return g.framesShown; }

}

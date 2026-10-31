#include "base/perf.hpp"
#include "base/dev-switches.hpp"
#include "base/log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace Perf {
namespace {

struct State {
    double frame[Count] = {};     // this frame so far
    double smooth[Count] = {};
    double worstNow[Count] = {}, worstShown[Count] = {};
    double sum[Count] = {};
    int    frames = 0, framesShown = 0;
    Clock::TimePoint windowStart = Clock::now();
    bool   print = devSwitches().perfLog;
};
State g;

constexpr const char* kNames[] = { "frame", "acquire", "stroke", "canvas", "canvas sync", "thumbnails", "graph" };
static_assert(std::size(kNames) == Count, "a name per Counter, in its order");

}

void add(Counter c, Clock::Seconds took) { g.frame[c] += Clock::Milliseconds(took).count(); }

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
    if (Clock::between(g.windowStart, now) < Clock::Seconds(1.0)) return;
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

Clock::Milliseconds average(Counter c) { return Clock::Milliseconds(g.smooth[c]); }
Clock::Milliseconds worst(Counter c)   { return Clock::Milliseconds(g.worstShown[c]); }
const char* name(Counter c) { return kNames[c]; }
int fps() { return g.framesShown; }

}

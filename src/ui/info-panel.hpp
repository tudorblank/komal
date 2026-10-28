#pragma once
// The Info window: the GPU and display, frame timings, the pen as last reported, and the perf
// counters (ms per frame, smoothed and worst of the last second).

#include <string>
#include <utility>

class ActionRegistry;
struct InputFrame;

class InfoPanel {
public:
    void draw(const InputFrame& input, ActionRegistry& actions);
    bool& open() { return m_open; }   // shown (View > Info)

    void setGpuInfo(std::string s) { m_gpuInfo = std::move(s); }
    void setTimings(float acquireMs, float cpuMs) { m_acquireMs = acquireMs; m_cpuMs = cpuMs; }

private:
    std::string m_gpuInfo;
    float m_acquireMs = 0, m_cpuMs = 0;
    bool  m_open = false;   // on demand
};

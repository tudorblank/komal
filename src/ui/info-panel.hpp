#pragma once
// The Info window: the GPU and display, the pen as last reported, and the perf counters (frame
// timings among them: ms per frame, smoothed and worst of the last second).

#include "ui/panel.hpp"


class InfoPanel : public Panel {
public:
    PanelInfo info() const override;
    bool init() override;
    void draw() override;

private:
};

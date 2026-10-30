#include "ui/info-panel.hpp"
#include "command/input.hpp"
#include "base/perf.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>

PanelInfo InfoPanel::info() const { return { Actions::ViewInfo, "Info", DockSlot::RightBottom, DrawPhase::Edit, false }; }   // on demand

bool InfoPanel::init()
{
    return true;
}

void InfoPanel::draw()
{
    if (!isOpen()) return;
    const InputFrame& input = ui().input;
    if (const Widgets::PanelWindow window(*this, ui().actions); window) {
        ImGui::TextWrapped("%s", ui().host.gpu.c_str());
        ImGui::Separator();
        ImGui::Text("%d frames in the last second", Perf::fps());   // only drawn when something changes
        if (input.pen.present) {
            const InputFrame::Pen& pen = input.pen;
            ImGui::Text("pen: %s %s  pressure %.2f  tilt %.0f %.0f", pen.eraser ? "eraser" : "tip",
                        pen.touching ? "down" : "up", pen.pressure, pen.tiltX, pen.tiltY);
        }
        ImGui::Separator();
        for (int c = 0; c < Perf::Count; ++c)   // ms per frame: smoothed, worst of the last second
            ImGui::Text("%-12s %6.2f  max %6.2f", Perf::name(Perf::Counter(c)), Perf::average(Perf::Counter(c)), Perf::worst(Perf::Counter(c)));
    }
}

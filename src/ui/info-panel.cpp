#include "ui/info-panel.hpp"
#include "core/input.hpp"
#include "core/perf.hpp"
#include "ui/panel-titles.hpp"
#include "ui/widgets.hpp"

#include <imgui.h>

void InfoPanel::draw(const InputFrame& input, ActionRegistry& actions)
{
    if (!m_open) return;
    if (Widgets::beginPanel(PanelTitle::kInfo, actions, "view.info")) {
        ImGui::TextWrapped("%s", m_gpuInfo.c_str());
        ImGui::Separator();
        ImGui::Text("acquire (vsync/WSI): %.2f ms", m_acquireMs);
        ImGui::Text("CPU work:            %.2f ms", m_cpuMs);
        ImGui::Text("%.1f FPS (%.3f ms)", ImGui::GetIO().Framerate,
                    1000.0f / ImGui::GetIO().Framerate);
        if (input.pen.present) {
            const InputFrame::Pen& pen = input.pen;
            ImGui::Text("pen: %s %s  pressure %.2f  tilt %.0f %.0f", pen.eraser ? "eraser" : "tip",
                        pen.touching ? "down" : "up", pen.pressure, pen.tiltX, pen.tiltY);
        }
        ImGui::Separator();
        for (int c = 0; c < Perf::Count; ++c)   // ms per frame: smoothed, worst of the last second
            ImGui::Text("%-12s %6.2f  max %6.2f", Perf::name(Perf::Counter(c)), Perf::average(Perf::Counter(c)), Perf::worst(Perf::Counter(c)));
    }
    Widgets::endPanel();
}

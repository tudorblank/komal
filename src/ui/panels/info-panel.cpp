#include "ui/panels/info-panel.hpp"
#include "base/perf.hpp"
#include "ui/panels/canvas/canvas-engine.hpp"
#include "input/input-frame.hpp"
#include "document/workspace.hpp"
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
        const auto mb = [](size_t bytes) { return double(bytes) / (1024.0 * 1024.0); };
        const NodeRegistry::PixelBytes px = ui().workspace().pixelBytes();
        ImGui::Text("pixels %.1f MB, their history %.1f MB", mb(px.pixels), mb(px.history));
        ImGui::Text("GPU tiles %.0f MB, thumbnails %.0f MB", mb(ui().canvas.tiles().bytes()), mb(ui().canvas.thumbnails().bytes()));
        ImGui::Separator();
        for (int c = 0; c < Perf::Count; ++c)   // ms per frame: smoothed, worst of the last second
            ImGui::Text("%-12s %6.2f  max %6.2f", Perf::name(Perf::Counter(c)), Perf::average(Perf::Counter(c)).count(), Perf::worst(Perf::Counter(c)).count());
    }
}

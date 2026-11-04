#include "ui/panels/history-panel.hpp"
#include "document/document-actions.hpp"
#include "command/actions.hpp"
#include "document/workspace.hpp"
#include "ui/render/painter.hpp"
#include "ui/style/ui-style.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// Steps are laid out one per row in the order they were made: a step's row is its id.
uint32_t rowOf(StepId step) { return uint32_t(step); }
StepId   stepAt(uint32_t row) { return StepId(row); }

}

void HistoryPanel::layout(const History& h)
{
    const uint32_t n = uint32_t(h.size());
    // Chains: a step and the first steps made after it, one after another. Each takes one lane,
    // from the row it branches off (its head's parent) to its last step.
    struct Chain { uint32_t start, end; int lane = 0; };
    std::vector<Chain> chains;
    std::vector<int> chainOf(n, 0);
    chains.push_back({ 0, 0 });
    for (uint32_t id = 1; id < n; ++id) {
        const uint32_t p = rowOf(h.parent(stepAt(id)));
        if (h.children(stepAt(p)).front() == stepAt(id)) {
            chainOf[id] = chainOf[p];
        } else {
            chainOf[id] = int(chains.size());
            chains.push_back({ p, id });
        }
        chains[size_t(chainOf[id])].end = id;
    }
    // Lanes in the order chains start: the lowest one that's free by then.
    std::vector<size_t> order(chains.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return chains[a].start < chains[b].start; });
    std::vector<uint32_t> laneEnd;   // the last row each lane is busy to
    for (size_t i : order) {
        Chain& c = chains[i];
        size_t lane = 0;
        while (lane < laneEnd.size() && laneEnd[lane] >= c.start) ++lane;
        if (lane == laneEnd.size()) laneEnd.push_back(0);
        laneEnd[lane] = c.end;
        c.lane = int(lane);
    }
    m_lane.resize(n);
    for (uint32_t id = 0; id < n; ++id) m_lane[id] = chains[size_t(chainOf[id])].lane;
    m_lanes   = int(laneEnd.size());
    m_laidOut = n;
}

PanelInfo HistoryPanel::info() const { return { Actions::ViewHistoryTree, "History Tree", DockSlot::RightBottom }; }

bool HistoryPanel::init()
{
    return true;
}

void HistoryPanel::documentsChanged(const DocumentsEvent& e)
{
    if (e.type != DocumentsEvent::Type::Activated) return;
    m_laidOut = 0;
    m_shownCurrent = kNoStep;
}

void HistoryPanel::draw()
{
    if (!isOpen()) return;
    const History& history = ui().workspace().history();
    ActionRegistry& actions = ui().actions;
    const Widgets::PanelWindow window(*this, actions);
    if (!window) return;
    if (history.size() != m_laidOut) layout(history);

    const Style::HistoryStyle& style = Style::get().history;
    const uint32_t count   = uint32_t(history.size());
    const uint32_t current = rowOf(history.current());
    const float font  = ImGui::GetFontSize();
    const float rowH  = style.rowHeight;
    const float laneW = style.laneWidth;
    const float dot   = style.dot;
    const float line  = style.line;
    const float ring  = style.ring;   // the current step's dot, the biggest thing drawn
    // Lane 0's dot sits half a lane in: the graph moves right by what the ring needs beyond that.
    const float inset = std::max(2.0f, std::ceil(ring - laneW * 0.5f) + 2.0f);

    // Where you are, the way back to the start, and redo's way on.
    std::vector<uint8_t> onPath(count, 0), onRedo(count, 0);
    for (StepId s = history.current(); s != kNoStep; s = history.parent(s)) onPath[rowOf(s)] = 1;
    for (StepId s = history.redoChild(history.current()); s != kNoStep; s = history.redoChild(s)) onRedo[rowOf(s)] = 1;

    ImGui::BeginChild("##tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float graphX = origin.x + inset;
    const float labelX = graphX + float(m_lanes) * laneW + style.labelGap;
    const float width  = std::max(ImGui::GetContentRegionAvail().x, labelX - origin.x + style.labelRoom);

    // One item over every row: the row under the mouse is the step to go to.
    ImGui::InvisibleButton("##steps", ImVec2(width, rowH * float(count)));
    const bool hovered = ImGui::IsItemHovered();
    const int hoveredRow = hovered ? int((ImGui::GetIO().MousePos.y - origin.y) / rowH) : -1;
    if (ImGui::IsItemClicked() && hoveredRow >= 0 && hoveredRow < int(count) && uint32_t(hoveredRow) != current)
        actions.run(Actions::HistoryGoTo, { { DocArgs::kStep, StepId(hoveredRow) } });

    // The current step stays in view when it changes (an undo, a jump, a new step).
    if (history.current() != m_shownCurrent) {
        const float top = float(current) * rowH, view = ImGui::GetWindowHeight();
        if (top < ImGui::GetScrollY() || top + rowH > ImGui::GetScrollY() + view)
            ImGui::SetScrollY(std::max(0.0f, top - (view - rowH) * 0.5f));
        m_shownCurrent = history.current();
    }

    // Only the rows on screen (and the links passing through them).
    const float scroll = ImGui::GetScrollY(), view = ImGui::GetWindowHeight();
    const uint32_t first = uint32_t(std::max(0.0f, std::floor(scroll / rowH)));
    const uint32_t last  = std::min(count - 1, uint32_t(std::ceil((scroll + view) / rowH)));

    const Painter draw = Painter::window();
    const ImU32 accent  = style.here;
    const ImU32 ahead   = style.ahead;
    const ImU32 other   = style.other;
    const ImU32 text    = style.text;
    const ImU32 dimText = style.textDim;
    const auto at = [&](uint32_t id) {
        return ImVec2(graphX + (float(m_lane[id]) + 0.5f) * laneW, origin.y + (float(id) + 0.5f) * rowH);
    };
    const auto tone = [&](uint32_t id) { return onPath[id] ? accent : onRedo[id] ? ahead : other; };

    if (hoveredRow >= int(first) && hoveredRow <= int(last))
        draw.fillRect(ImVec2(origin.x, origin.y + float(hoveredRow) * rowH), ImVec2(origin.x + width, origin.y + float(hoveredRow + 1) * rowH), style.rowHot, style.rounding);

    // Links: from each step's parent, straight down its own lane, or curving out into a branch's.
    for (uint32_t id = 1; id < count; ++id) {
        const uint32_t p = rowOf(history.parent(stepAt(id)));
        if (id < first || p > last) continue;
        const ImVec2 a = at(p), b = at(id);
        const ImU32 col = tone(id);
        if (m_lane[p] == m_lane[id]) {
            draw.line(a, b, col, line);
        } else {
            const ImVec2 turn(b.x, a.y + rowH * 0.8f);
            draw.bezier(a, ImVec2(b.x, a.y), turn, col, line);
            draw.line(turn, b, col, line);
        }
    }

    // Steps: a dot each, its label after every lane.
    for (uint32_t id = first; id <= last; ++id) {
        const ImVec2 c = at(id);
        if (id == current) {
            draw.fillCircle(c, ring, accent);
            draw.fillCircle(c, dot, text);
        } else if (onPath[id]) {
            draw.fillCircle(c, dot, accent);
        } else if (onRedo[id]) {
            draw.fillCircle(c, dot, style.hollow);
            draw.strokeCircle(c, dot, ahead, line * 1.5f);
        } else {
            draw.fillCircle(c, dot * 0.85f, other);
        }
        const HistoryStep& s = history.step(stepAt(id));
        const float ty = c.y - font * 0.5f;
        draw.text(ImVec2(labelX, ty), id == current || onPath[id] ? text : dimText, s.label.c_str());
        char num[16];
        std::snprintf(num, sizeof num, "%u", id);
        const float nw = ImGui::CalcTextSize(num).x;
        draw.text(ImVec2(origin.x + width - nw - style.numberPadding, ty), dimText, num);
    }
    ImGui::EndChild();
}

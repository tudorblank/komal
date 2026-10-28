#include "ui/history-panel.hpp"
#include "core/actions.hpp"
#include "core/history.hpp"
#include "ui/panel-titles.hpp"
#include "ui/widgets.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

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
        const uint32_t p = h.parent(id);
        if (h.children(p).front() == id) {
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

void HistoryPanel::draw(const History& history, ActionRegistry& actions)
{
    if (!m_open) return;
    if (!Widgets::beginPanel(PanelTitle::kHistoryTree, actions, "view.historyTree")) {
        Widgets::endPanel();
        return;
    }
    if (history.size() != m_laidOut) layout(history);

    const uint32_t count   = uint32_t(history.size());
    const uint32_t current = history.current();
    const float font  = ImGui::GetFontSize();
    const float rowH  = std::round(font * 1.55f);
    const float laneW = std::round(font * 0.95f);
    const float dot   = std::max(3.0f, std::round(font * 0.24f));
    const float line  = std::max(1.0f, std::round(font * 0.1f));
    const float ring  = dot + line * 2.5f;   // the current step's dot, the biggest thing drawn
    // Lane 0's dot sits half a lane in: the graph moves right by what the ring needs beyond that.
    const float inset = std::max(2.0f, std::ceil(ring - laneW * 0.5f) + 2.0f);

    // Where you are, the way back to the start, and redo's way on.
    std::vector<uint8_t> onPath(count, 0), onRedo(count, 0);
    for (uint32_t id = current; id != History::kNone; id = history.parent(id)) onPath[id] = 1;
    for (uint32_t id = history.redoChild(current); id != History::kNone; id = history.redoChild(id)) onRedo[id] = 1;

    ImGui::BeginChild("##tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float graphX = origin.x + inset;
    const float labelX = graphX + float(m_lanes) * laneW + std::round(font * 0.5f);
    const float width  = std::max(ImGui::GetContentRegionAvail().x, labelX - origin.x + font * 12.0f);

    // One item over every row: the row under the mouse is the step to go to.
    ImGui::InvisibleButton("##steps", ImVec2(width, rowH * float(count)));
    const bool hovered = ImGui::IsItemHovered();
    const int hoveredRow = hovered ? int((ImGui::GetIO().MousePos.y - origin.y) / rowH) : -1;
    if (ImGui::IsItemClicked() && hoveredRow >= 0 && hoveredRow < int(count) && uint32_t(hoveredRow) != current)
        actions.run("history.goTo", { { "node", hoveredRow } });

    // The current step stays in view when it changes (an undo, a jump, a new step).
    if (current != m_shownCurrent) {
        const float top = float(current) * rowH, view = ImGui::GetWindowHeight();
        if (top < ImGui::GetScrollY() || top + rowH > ImGui::GetScrollY() + view)
            ImGui::SetScrollY(std::max(0.0f, top - (view - rowH) * 0.5f));
        m_shownCurrent = current;
    }

    // Only the rows on screen (and the links passing through them).
    const float scroll = ImGui::GetScrollY(), view = ImGui::GetWindowHeight();
    const uint32_t first = uint32_t(std::max(0.0f, std::floor(scroll / rowH)));
    const uint32_t last  = std::min(count - 1, uint32_t(std::ceil((scroll + view) / rowH)));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 accent  = ImGui::GetColorU32(ImGuiCol_CheckMark);
    const ImU32 ahead   = ImGui::GetColorU32(ImGuiCol_CheckMark, 0.45f);
    const ImU32 other   = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.7f);
    const ImU32 text    = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dimText = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const auto at = [&](uint32_t id) {
        return ImVec2(graphX + (float(m_lane[id]) + 0.5f) * laneW, origin.y + (float(id) + 0.5f) * rowH);
    };
    const auto tone = [&](uint32_t id) { return onPath[id] ? accent : onRedo[id] ? ahead : other; };

    if (hoveredRow >= int(first) && hoveredRow <= int(last))
        dl->AddRectFilled(ImVec2(origin.x, origin.y + float(hoveredRow) * rowH), ImVec2(origin.x + width, origin.y + float(hoveredRow + 1) * rowH),
                          ImGui::GetColorU32(ImGuiCol_HeaderHovered), ImGui::GetStyle().FrameRounding);

    // Links: from each step's parent, straight down its own lane, or curving out into a branch's.
    for (uint32_t id = 1; id < count; ++id) {
        const uint32_t p = history.parent(id);
        if (id < first || p > last) continue;
        const ImVec2 a = at(p), b = at(id);
        const ImU32 col = tone(id);
        if (m_lane[p] == m_lane[id]) {
            dl->AddLine(a, b, col, line);
        } else {
            const ImVec2 turn(b.x, a.y + rowH * 0.8f);
            dl->AddBezierQuadratic(a, ImVec2(b.x, a.y), turn, col, line);
            dl->AddLine(turn, b, col, line);
        }
    }

    // Steps: a dot each, its label after every lane.
    for (uint32_t id = first; id <= last; ++id) {
        const ImVec2 c = at(id);
        if (id == current) {
            dl->AddCircleFilled(c, ring, accent);
            dl->AddCircleFilled(c, dot, text);
        } else if (onPath[id]) {
            dl->AddCircleFilled(c, dot, accent);
        } else if (onRedo[id]) {
            dl->AddCircleFilled(c, dot, ImGui::GetColorU32(ImGuiCol_WindowBg));
            dl->AddCircle(c, dot, ahead, 0, line * 1.5f);
        } else {
            dl->AddCircleFilled(c, dot * 0.85f, other);
        }
        const HistoryStep& s = history.step(id);
        const float ty = c.y - font * 0.5f;
        dl->AddText(ImVec2(labelX, ty), id == current || onPath[id] ? text : dimText, s.label.c_str());
        char num[16];
        std::snprintf(num, sizeof num, "%u", id);
        const float nw = ImGui::CalcTextSize(num).x;
        dl->AddText(ImVec2(origin.x + width - nw - font * 0.4f, ty), dimText, num);
    }
    ImGui::EndChild();
    Widgets::endPanel();
}

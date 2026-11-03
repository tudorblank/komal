#pragma once
// How the widgets take the mouse and the keyboard, shared by their files (not for panels: they
// use widgets.hpp): where a control's item and box go on its row, a slider's travel, and the one
// field being typed into. No colours and no drawing: that's each control's skin (ui/render).

#include "ui/style/ui-style.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // ImRect, FindRenderedTextEnd, ClearActiveID
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Widgets::detail {

// The box every field, button and swatch is drawn in: boxHeight tall, centred in a row at `p`.
inline ImRect boxAt(ImVec2 p, float width)
{
    const Style::Metrics& m = Style::metrics();
    const float y = std::round(p.y + (m.rowHeight - m.boxHeight) * 0.5f);
    return ImRect(ImVec2(p.x, y), ImVec2(p.x + width, y + m.boxHeight));
}

// A row-tall item `width` wide; returns its box.
inline ImRect boxItem(const char* id, float width, bool& clicked, bool& hot)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    clicked = ImGui::InvisibleButton(id, ImVec2(width, Style::metrics().rowHeight));
    hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    return boxAt(p, width);
}

// A slider's item and the grab's travel (x0..x1 keeps the grab inside the item). While active,
// `t` follows the mouse (0..1); returns whether it does.
struct Track { float x0, x1, cy; bool hot; };
inline bool trackItem(const char* id, float width, float& t, Track& out, float margin)   // margin: kept clear at each end
{
    const Style::Metrics& m = Style::metrics();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    width = std::max(width, 8.0f);
    ImGui::InvisibleButton(id, ImVec2(width, m.rowHeight));
    out = { p.x + margin, p.x + width - margin, std::round(p.y + m.rowHeight * 0.5f), ImGui::IsItemHovered() || ImGui::IsItemActive() };
    if (!ImGui::IsItemActive()) return false;
    t = std::clamp((ImGui::GetIO().MousePos.x - out.x0) / std::max(1.0f, out.x1 - out.x0), 0.0f, 1.0f);
    return true;
}

// A double-click on the item just drawn puts `v` back to its default (if it has one). True then.
inline bool resetOnDoubleClick(float& v, float min, float max, float defaultValue)
{
    if (std::isnan(defaultValue) || !ImGui::IsItemHovered() || !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) return false;
    v = std::clamp(defaultValue, min, max);   // after the click that moved it
    ImGui::ClearActiveID();                   // no drag while the button is still down
    return true;
}

// A value range mapped to 0..1 along a track, evenly or logarithmically.
struct Range {
    float min, max;
    bool  log;
    Range(float lo, float hi, bool logarithmic) : min(lo), max(hi), log(logarithmic && lo > 0.0f && hi > lo) {}
    float toT(float x) const { return log ? std::log(x / min) / std::log(max / min) : (x - min) / (max - min); }
    float fromT(float t) const { return log ? min * std::pow(max / min, t) : min + (max - min) * t; }
};

// The value being typed: one field at a time.
struct TextEdit {
    ImGuiID id = 0;
    int     frames = 0;   // since it started (focus takes a frame to land)
    char    text[128] = "";
};
inline TextEdit g_edit;   // the one field being typed in
inline ImGuiID g_focusRequest = 0;   // focusField()

inline void beginEdit(ImGuiID id, const char* text)
{
    g_edit.id = id;
    g_edit.frames = 0;
    std::snprintf(g_edit.text, sizeof g_edit.text, "%s", text);
}

// Takes a focusField() request for `id`: starts typing `text` into it.
inline void takeFocusRequest(ImGuiID id, const char* text)
{
    if (g_focusRequest != id) return;
    g_focusRequest = 0;
    beginEdit(id, text);
}

// ImGui's text input, shaped like a box: what typing happens in.
inline void boxShapedInput(float width)
{
    const Style::Metrics& m = Style::metrics();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(m.framePadding, (m.boxHeight - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, m.rounding);
    ImGui::SetNextItemWidth(width);
}

// While the box `id` is being typed into, draws the text field in its place and returns true.
// `apply` gets the text when the field is left with Enter or a click away (not Escape).
template<class Apply>
inline bool editBox(ImGuiID id, ImVec2 rowPos, float width, ImGuiInputTextFlags flags, Apply&& apply)
{
    if (g_edit.id != id) return false;
    if (g_edit.frames++ == 0) ImGui::SetKeyboardFocusHere();
    ImGui::SetCursorScreenPos(boxAt(rowPos, width).Min);
    boxShapedInput(width);
    ImGui::PushID(int(id));
    ImGui::InputText("##edit", g_edit.text, sizeof g_edit.text, flags | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopID();
    ImGui::PopStyleVar(2);
    const bool applied = ImGui::IsItemDeactivatedAfterEdit();
    const bool left = applied || (!ImGui::IsItemActive() && g_edit.frames > 2);
    if (applied) apply(g_edit.text);
    if (left) g_edit.id = 0;
    ImGui::SetCursorScreenPos(rowPos);
    ImGui::Dummy(ImVec2(width, Style::metrics().rowHeight));   // the row keeps its height
    return true;
}

// A toggle's item: its mark plus its label, a row tall.
struct ToggleItem {
    ImVec2      mark;    // the mark's centre
    ImVec2      label;   // where its label starts
    const char* end;     // the label's visible end
    bool        hasLabel, pressed, hovered;
};
inline ToggleItem toggleItem(const char* label)
{
    const Style::Styles& s = Style::get();
    const float mark = s.toggle.mark, h = s.metrics.rowHeight, gap = s.metrics.innerSpacing;
    ToggleItem t{};
    t.end = ImGui::FindRenderedTextEnd(label);
    const float textW = ImGui::CalcTextSize(label, t.end).x;
    t.hasLabel = textW > 0.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    t.pressed = ImGui::InvisibleButton(label, ImVec2(mark + (t.hasLabel ? gap + textW : 0.0f), h));
    t.hovered = ImGui::IsItemHovered();
    t.mark  = ImVec2(std::floor(p.x + mark * 0.5f) + 0.5f, std::floor(p.y + h * 0.5f) + 0.5f);
    t.label = ImVec2(p.x + mark + gap, p.y + (h - ImGui::GetFontSize()) * 0.5f);
    return t;
}

}

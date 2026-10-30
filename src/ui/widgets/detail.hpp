#pragma once
// What the widgets are made of, shared by their files (not for panels: they use widgets.hpp).

#include "base/palette.hpp"
#include "ui/widgets/widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>   // ImRect, FindRenderedTextEnd, ClearActiveID
#include <algorithm>
#include <cmath>

namespace Widgets::detail {

// Slider and toggle colours, from the palette.
constexpr ImU32 kTrack      = Palette::kTrack.packed();
constexpr ImU32 kTrackFill  = Palette::kAccentDim.packed();
constexpr ImU32 kGrab       = Palette::kGrab.packed();
constexpr ImU32 kGrabHot    = Palette::kGrabHot.packed();
constexpr ImU32 kGrabBorder = Palette::kGrabRim.packed();
constexpr ImU32 kToggleEdge = Palette::kToggleEdge.packed();
constexpr ImU32 kToggleHot  = Palette::kToggleHot.packed();

inline ImU32 accent() { return ImGui::GetColorU32(ImGuiCol_CheckMark); }

// The box every field, button and swatch is drawn in: boxHeight tall, centred in a row at `p`.
inline ImRect boxAt(ImVec2 p, float width)
{
    const Widgets::Metrics m = Widgets::metrics();
    const float y = std::round(p.y + (m.rowHeight - m.boxHeight) * 0.5f);
    return ImRect(ImVec2(p.x, y), ImVec2(p.x + width, y + m.boxHeight));
}

inline void drawBox(const ImRect& r, bool hot)
{
    ImGui::GetWindowDrawList()->AddRectFilled(r.Min, r.Max, ImGui::GetColorU32(hot ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg),
                                              Widgets::metrics().rounding);
}

// A row-tall item `width` wide; returns its box.
inline ImRect boxItem(const char* id, float width, bool& clicked, bool& hot)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    clicked = ImGui::InvisibleButton(id, ImVec2(width, Widgets::metrics().rowHeight));
    hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    return boxAt(p, width);
}

inline void textCentered(const ImRect& r, const char* text, const char* end, ImU32 color)
{
    const ImVec2 t = ImGui::CalcTextSize(text, end);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(r.Min, r.Max, true);
    dl->AddText(ImVec2(std::round((r.Min.x + r.Max.x - t.x) * 0.5f), std::round((r.Min.y + r.Max.y - t.y) * 0.5f)), color, text, end);
    dl->PopClipRect();
}

// The slider grab: a small pill with a dark outline, centred on (x, cy).
inline void drawGrab(float x, float cy, bool hot, float halfHeight = 0.0f)   // 0: the standard height
{
    const float u = Widgets::metrics().unit;
    const float gw = std::round(3.0f * u), gh = halfHeight > 0.0f ? halfHeight : std::round(6.5f * u);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(x - gw - 1.0f, cy - gh - 1.0f), ImVec2(x + gw + 1.0f, cy + gh + 1.0f), kGrabBorder, 2.0f * u);
    dl->AddRectFilled(ImVec2(x - gw, cy - gh), ImVec2(x + gw, cy + gh), hot ? kGrabHot : kGrab, 1.5f * u);
}

// A slider's item and the grab's travel (x0..x1 keeps the grab inside the item). While active,
// `t` follows the mouse (0..1); returns whether it does.
struct Track { float x0, x1, cy; bool hot; };
inline bool trackItem(const char* id, float width, float& t, Track& out, float margin = -1.0f)   // margin: kept clear at each end
{
    const Widgets::Metrics m = Widgets::metrics();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    width = std::max(width, 8.0f);
    ImGui::InvisibleButton(id, ImVec2(width, m.rowHeight));
    if (margin < 0.0f) margin = std::round(3.0f * m.unit) + 1.0f;   // the small grab's half width
    out = { p.x + margin, p.x + width - margin, std::round(p.y + m.rowHeight * 0.5f), ImGui::IsItemHovered() || ImGui::IsItemActive() };
    if (!ImGui::IsItemActive()) return false;
    t = std::clamp((ImGui::GetIO().MousePos.x - out.x0) / std::max(1.0f, out.x1 - out.x0), 0.0f, 1.0f);
    return true;
}

// The value being typed: one field at a time.
struct TextEdit {
    ImGuiID id = 0;
    int     frames = 0;   // since it started (focus takes a frame to land)
    char    text[128] = "";
};
inline TextEdit g_edit;   // the one field being typed in
inline ImGuiID g_focusRequest = 0;   // focusField()

// Takes a focusField() request for `id`: starts typing `text` into it.
inline void takeFocusRequest(ImGuiID id, const char* text)
{
    if (g_focusRequest != id) return;
    g_focusRequest = 0;
    g_edit.id = id;
    g_edit.frames = 0;
    std::snprintf(g_edit.text, sizeof g_edit.text, "%s", text);
}

inline void beginEdit(ImGuiID id, const char* text)
{
    g_edit.id = id;
    g_edit.frames = 0;
    std::snprintf(g_edit.text, sizeof g_edit.text, "%s", text);
}

// While the box `id` is being typed into, draws the text field in its place and returns true.
// `apply` gets the text when the field is left with Enter or a click away (not Escape).
template<class Apply>
inline bool editBox(ImGuiID id, ImVec2 rowPos, float width, ImGuiInputTextFlags flags, Apply&& apply)
{
    if (g_edit.id != id) return false;
    const Widgets::Metrics m = Widgets::metrics();
    if (g_edit.frames++ == 0) ImGui::SetKeyboardFocusHere();
    ImGui::SetCursorScreenPos(boxAt(rowPos, width).Min);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, (m.boxHeight - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, m.rounding);
    ImGui::SetNextItemWidth(width);
    ImGui::PushID(int(id));
    ImGui::InputText("##edit", g_edit.text, sizeof g_edit.text, flags | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopID();
    ImGui::PopStyleVar(2);
    const bool applied = ImGui::IsItemDeactivatedAfterEdit();
    const bool left = applied || (!ImGui::IsItemActive() && g_edit.frames > 2);
    if (applied) apply(g_edit.text);
    if (left) g_edit.id = 0;
    ImGui::SetCursorScreenPos(rowPos);
    ImGui::Dummy(ImVec2(width, m.rowHeight));   // the row keeps its height
    return true;
}

// A toggle's item: the mark plus its label, a row tall. Draws the label; returns the mark's centre.
inline ImVec2 toggleItem(const char* label, float mark, bool& pressed, bool& hovered)
{
    const char* end = ImGui::FindRenderedTextEnd(label);
    const float gap = std::round(ImGui::GetStyle().ItemInnerSpacing.x);
    const float textW = ImGui::CalcTextSize(label, end).x;
    const float h = Widgets::metrics().rowHeight;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    pressed = ImGui::InvisibleButton(label, ImVec2(mark + (textW > 0.0f ? gap + textW : 0.0f), h));
    hovered = ImGui::IsItemHovered();
    if (textW > 0.0f)
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + mark + gap, p.y + (h - ImGui::GetFontSize()) * 0.5f),
                                            ImGui::GetColorU32(ImGuiCol_Text), label, end);
    return ImVec2(std::floor(p.x + mark * 0.5f) + 0.5f, std::floor(p.y + h * 0.5f) + 0.5f);
}

}

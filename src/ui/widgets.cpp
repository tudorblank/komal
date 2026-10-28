#include "ui/widgets.hpp"
#include "core/actions.hpp"
#include "ui/theme.hpp"

#include <imgui_internal.h>   // ImRect, FindRenderedTextEnd, ClearActiveID
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr float kPi = 3.14159265358979f;

// Slider colours (the graph nodes'), the track lifted to read on a panel.
constexpr ImU32 kTrack      = IM_COL32( 50,  50,  50, 255);
constexpr ImU32 kTrackFill  = IM_COL32(104, 136, 186, 255);
constexpr ImU32 kGrab       = IM_COL32(205, 205, 205, 255);
constexpr ImU32 kGrabHot    = IM_COL32(255, 255, 255, 255);
constexpr ImU32 kGrabBorder = IM_COL32( 10,  10,  10, 255);
constexpr ImU32 kToggleEdge = IM_COL32( 92,  92,  92, 255);
constexpr ImU32 kToggleHot  = IM_COL32(140, 140, 140, 255);

ImU32 accent() { return ImGui::GetColorU32(ImGuiCol_CheckMark); }

// The box every field, button and swatch is drawn in: boxHeight tall, centred in a row at `p`.
ImRect boxAt(ImVec2 p, float width)
{
    const Widgets::Metrics m = Widgets::metrics();
    const float y = std::round(p.y + (m.rowHeight - m.boxHeight) * 0.5f);
    return ImRect(ImVec2(p.x, y), ImVec2(p.x + width, y + m.boxHeight));
}

void drawBox(const ImRect& r, bool hot)
{
    ImGui::GetWindowDrawList()->AddRectFilled(r.Min, r.Max, ImGui::GetColorU32(hot ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg),
                                              Widgets::metrics().rounding);
}

// A row-tall item `width` wide; returns its box.
ImRect boxItem(const char* id, float width, bool& clicked, bool& hot)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    clicked = ImGui::InvisibleButton(id, ImVec2(width, Widgets::metrics().rowHeight));
    hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    return boxAt(p, width);
}

void textCentered(const ImRect& r, const char* text, const char* end, ImU32 color)
{
    const ImVec2 t = ImGui::CalcTextSize(text, end);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(r.Min, r.Max, true);
    dl->AddText(ImVec2(std::round((r.Min.x + r.Max.x - t.x) * 0.5f), std::round((r.Min.y + r.Max.y - t.y) * 0.5f)), color, text, end);
    dl->PopClipRect();
}

// The slider grab: a small pill with a dark outline, centred on (x, cy).
void drawGrab(float x, float cy, bool hot, float halfHeight = 0.0f)   // 0: the standard height
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
bool trackItem(const char* id, float width, float& t, Track& out, float margin = -1.0f)   // margin: kept clear at each end
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
TextEdit g_edit;
ImGuiID  g_focusRequest = 0;   // focusField()

// Takes a focusField() request for `id`: starts typing `text` into it.
void takeFocusRequest(ImGuiID id, const char* text)
{
    if (g_focusRequest != id) return;
    g_focusRequest = 0;
    g_edit.id = id;
    g_edit.frames = 0;
    std::snprintf(g_edit.text, sizeof g_edit.text, "%s", text);
}

void beginEdit(ImGuiID id, const char* text)
{
    g_edit.id = id;
    g_edit.frames = 0;
    std::snprintf(g_edit.text, sizeof g_edit.text, "%s", text);
}

// While the box `id` is being typed into, draws the text field in its place and returns true.
// `apply` gets the text when the field is left with Enter or a click away (not Escape).
template<class Apply>
bool editBox(ImGuiID id, ImVec2 rowPos, float width, ImGuiInputTextFlags flags, Apply&& apply)
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
ImVec2 toggleItem(const char* label, float mark, bool& pressed, bool& hovered)
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

namespace Widgets {

Metrics metrics()
{
    const float font = ImGui::GetFontSize(), u = font / 15.0f;
    Metrics m;
    m.unit            = u;
    m.rowHeight       = ImGui::GetFrameHeight();
    m.boxHeight       = std::round(font + 6.0f * u);
    m.fieldWidth      = std::round(font * 3.6f);
    m.labelWidth      = std::round(font * 5.0f);
    m.shortLabelWidth = std::round(font * 1.1f);
    m.spacing         = ImGui::GetStyle().ItemSpacing.x;
    m.groupSpacing    = std::round(font);
    m.rounding        = std::round(3.0f * u);
    m.panelPadding    = std::round(10.0f * u);
    m.sectionGap      = std::round(6.0f * u);
    return m;
}

// ---------------------------------------------------------------- panels

bool beginPanel(const char* name, ActionRegistry& actions, const char* closeAction, ImGuiWindowFlags flags)
{
    const float p = metrics().panelPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(p, p));
    bool keep = true;
    const bool shown = ImGui::Begin(name, &keep, flags);
    ImGui::PopStyleVar();
    // A docked panel's tab bar ends in a separator in the selected tab's colour (the panel's):
    // the top padding counts from the tab bar, like every other side.
    if (shown && ImGui::IsWindowDocked()) ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().TabBarBorderSize);
    if (!keep) actions.run(closeAction);
    return shown;
}

void endPanel()
{
    ImGui::End();
}

void sectionGap()
{
    ImGui::Dummy(ImVec2(0.0f, metrics().sectionGap));
}

float stackHeight(int rows, int headings, int gaps)
{
    const Metrics m = metrics();
    const float sy = ImGui::GetStyle().ItemSpacing.y;
    // Each item is followed by the item spacing; the last one's is dropped (the panel's padding follows).
    const int items = rows + headings + gaps;
    return rows * m.rowHeight + headings * ImGui::GetTextLineHeight() + gaps * m.sectionGap + std::max(0, items - 1) * sy;
}

// ---------------------------------------------------------------- layout

void heading(const char* text)
{
    ImGui::TextUnformatted(text);
}

void rowLabel(const char* text, float width)
{
    const float x = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(x + (width > 0.0f ? width : metrics().labelWidth));
}

void sameRow()
{
    ImGui::SameLine(0.0f, metrics().spacing);
}

void nextGroup()
{
    ImGui::SameLine(0.0f, metrics().groupSpacing);
}

float stretchWidth(float reservedRight)
{
    return std::max(8.0f, ImGui::GetContentRegionAvail().x - (reservedRight > 0.0f ? reservedRight + metrics().spacing : 0.0f));
}

void alignRight(float width)
{
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
}

// ---------------------------------------------------------------- controls

bool button(const char* label, float width)
{
    const char* end = ImGui::FindRenderedTextEnd(label);
    if (width <= 0.0f) width = std::round(ImGui::CalcTextSize(label, end).x + 2.0f * ImGui::GetStyle().FramePadding.x);
    bool clicked = false, hot = false;
    const ImRect box = boxItem(label, width, clicked, hot);
    drawBox(box, hot);
    textCentered(box, label, end, ImGui::GetColorU32(ImGuiCol_Text));
    return clicked;
}

bool iconButton(const char* id, Icon icon)
{
    const Metrics m = metrics();
    bool clicked = false, hot = false;
    const ImRect box = boxItem(id, m.boxHeight, clicked, hot);
    drawBox(box, hot);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c = box.GetCenter();
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    const float s = m.boxHeight * 0.22f, t = std::max(1.25f, m.boxHeight * 0.08f);
    switch (icon) {
    case Icon::Plus:
        dl->AddLine(ImVec2(c.x, c.y - s), ImVec2(c.x, c.y + s), color, t);
        [[fallthrough]];
    case Icon::Minus:
        dl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), color, t);
        break;
    case Icon::RotateLeft:
    case Icon::RotateRight: {
        // ↻: an open circle, gap at the top, its arrowhead there pointing clockwise. ↺ mirrors it.
        const float mirror = icon == Icon::RotateLeft ? -1.0f : 1.0f;
        const auto at = [&](ImVec2 p) { return ImVec2(c.x + (p.x - c.x) * mirror, p.y + s * 0.15f); };
        const float r = s * 1.1f, a0 = -kPi * 0.11f, a1 = a0 + kPi * 1.62f;
        for (int i = 0; i <= 20; ++i) {
            const float k = a0 + (a1 - a0) * float(i) / 20.0f;
            dl->PathLineTo(at(ImVec2(c.x + std::cos(k) * r, c.y + std::sin(k) * r)));
        }
        dl->PathStroke(color, 0, t);
        const ImVec2 end(c.x + std::cos(a1) * r, c.y + std::sin(a1) * r);
        const ImVec2 along(-std::sin(a1), std::cos(a1)), out(std::cos(a1), std::sin(a1));   // clockwise tangent, outwards
        const float h = s * 0.75f;
        dl->AddTriangleFilled(at(ImVec2(end.x + along.x * h, end.y + along.y * h)),
                              at(ImVec2(end.x + out.x * h * 0.65f - along.x * h * 0.2f, end.y + out.y * h * 0.65f - along.y * h * 0.2f)),
                              at(ImVec2(end.x - out.x * h * 0.65f - along.x * h * 0.2f, end.y - out.y * h * 0.65f - along.y * h * 0.2f)), color);
        break;
    }
    }
    return clicked;
}

bool swatch(const char* id, const float rgba[4])
{
    const Metrics m = metrics();
    bool clicked = false, hot = false;
    const ImRect box = boxItem(id, m.boxHeight, clicked, hot);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(box.Min, box.Max, ImGui::ColorConvertFloat4ToU32(ImVec4(rgba[0], rgba[1], rgba[2], 1.0f)), m.rounding);
    dl->AddRect(box.Min, box.Max, hot ? kToggleHot : IM_COL32(0, 0, 0, 160), m.rounding, 0, 1.0f);
    return clicked;
}

bool slider(const char* id, float& v, float min, float max, float width, bool logarithmic, float defaultValue)
{
    const bool log = logarithmic && min > 0.0f && max > min;
    const auto toT   = [&](float x) { return log ? std::log(x / min) / std::log(max / min) : (x - min) / (max - min); };
    const auto fromT = [&](float t) { return log ? min * std::pow(max / min, t) : min + (max - min) * t; };

    float t = 0.0f;
    Track tr{};
    bool changed = false;
    if (trackItem(id, width, t, tr)) {
        const float nv = std::clamp(fromT(t), min, max);
        if (nv != v) { v = nv; changed = true; }
    }
    if (!std::isnan(defaultValue) && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        v = std::clamp(defaultValue, min, max);   // after the click above moved it
        changed = true;
        ImGui::ClearActiveID();                   // no drag while the button is still down
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float th = std::max(2.0f, std::round(3.0f * metrics().unit));
    const float xv = std::round(tr.x0 + (tr.x1 - tr.x0) * std::clamp(toT(v), 0.0f, 1.0f));
    dl->AddRectFilled(ImVec2(tr.x0, tr.cy - th * 0.5f), ImVec2(tr.x1, tr.cy + th * 0.5f), kTrack, th * 0.5f);
    dl->AddRectFilled(ImVec2(tr.x0, tr.cy - th * 0.5f), ImVec2(xv, tr.cy + th * 0.5f), kTrackFill, th * 0.5f);
    drawGrab(xv, tr.cy, tr.hot);
    return changed;
}

bool figureSlider(const char* id, float& v, float min, float max, float width, SliderFigure figure, bool logarithmic, float defaultValue)
{
    const bool log = logarithmic && min > 0.0f && max > min;
    const auto toT   = [&](float x) { return log ? std::log(x / min) / std::log(max / min) : (x - min) / (max - min); };
    const auto fromT = [&](float t) { return log ? min * std::pow(max / min, t) : min + (max - min) * t; };

    const Metrics m = metrics();
    const float grabHalfW = std::round(3.0f * m.unit) + 1.0f;   // the standard grab, with its rim
    float t = 0.0f;
    Track tr{};
    bool changed = false;
    if (trackItem(id, width, t, tr, grabHalfW + std::round(2.0f * m.unit))) {
        const float nv = std::clamp(fromT(t), min, max);
        if (nv != v) { v = nv; changed = true; }
    }
    if (!std::isnan(defaultValue) && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        v = std::clamp(defaultValue, min, max);   // after the click above moved it
        changed = true;
        ImGui::ClearActiveID();                   // no drag while the button is still down
    }

    // A field's box, slimmer than the fields beside it; the grab a little taller than the box.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float margin = grabHalfW + std::round(2.0f * m.unit);
    const float boxH   = std::round(m.boxHeight * 0.62f);
    const ImRect box(ImVec2(tr.x0 - margin, tr.cy - boxH * 0.5f), ImVec2(tr.x1 + margin, tr.cy + boxH * 0.5f));
    drawBox(box, tr.hot);

    // The figure, filled in the track's grey; the part the value covers in the slider blue: up to
    // the grab, or for a value either side of zero (a Wave), from the middle to it.
    const float pad = std::round(2.0f * m.unit);
    const float fx0 = box.Min.x + pad, fx1 = box.Max.x - pad, fy0 = box.Min.y + pad, fy1 = box.Max.y - pad;
    const auto drawFigure = [&](ImU32 col) {
        if (figure == SliderFigure::Ramp) {   // a wedge: a sliver at the left, the full height at the right
            const float sliver = std::max(1.0f, std::round(m.unit));
            dl->AddQuadFilled(ImVec2(fx0, fy1 - sliver), ImVec2(fx1, fy0), ImVec2(fx1, fy1), ImVec2(fx0, fy1), col);
            return;
        }
        // One turn of a sine, filled down to the bottom (one shape: no seams): low left of the
        // middle, high right of it.
        constexpr int kSteps = 48;
        const float mid = (fy0 + fy1) * 0.5f, amp = (fy1 - fy0) * 0.38f;
        ImVec2 pts[kSteps + 3];
        for (int i = 0; i <= kSteps; ++i) {
            const float s = float(i) / kSteps;
            pts[i] = ImVec2(fx0 + (fx1 - fx0) * s, mid - std::sin((s * 2.0f - 1.0f) * kPi) * amp);
        }
        pts[kSteps + 1] = ImVec2(fx1, fy1);
        pts[kSteps + 2] = ImVec2(fx0, fy1);
        dl->AddConcavePolyFilled(pts, kSteps + 3, col);
    };
    const float xv = std::round(tr.x0 + (tr.x1 - tr.x0) * std::clamp(toT(v), 0.0f, 1.0f));
    const float from = figure == SliderFigure::Wave ? std::round(tr.x0 + (tr.x1 - tr.x0) * std::clamp(toT(0.0f), 0.0f, 1.0f)) : fx0;
    drawFigure(kTrack);
    dl->PushClipRect(ImVec2(std::min(from, xv), fy0), ImVec2(std::max(from, xv), fy1), true);
    drawFigure(kTrackFill);
    dl->PopClipRect();

    drawGrab(xv, tr.cy, tr.hot, std::round(m.boxHeight * 0.38f));
    return changed;
}

bool gradientSlider(const char* id, float& v, const ImU32* stops, int count, float width)
{
    float t = 0.0f;
    Track tr{};
    bool changed = false;
    if (trackItem(id, width, t, tr) && t != v) { v = t; changed = true; }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float th = std::round(8.0f * metrics().unit);
    const float y0 = tr.cy - th * 0.5f, y1 = tr.cy + th * 0.5f, len = tr.x1 - tr.x0;
    for (int i = 0; i + 1 < count; ++i) {
        const float a = tr.x0 + len * float(i) / float(count - 1), b = tr.x0 + len * float(i + 1) / float(count - 1);
        dl->AddRectFilledMultiColor(ImVec2(a, y0), ImVec2(b, y1), stops[i], stops[i + 1], stops[i + 1], stops[i]);
    }
    // Rounded ends: the panel colour drawn over the gradient's corners.
    dl->AddRect(ImVec2(tr.x0 - 1.0f, y0 - 1.0f), ImVec2(tr.x1 + 1.0f, y1 + 1.0f), ImGui::GetColorU32(ImGuiCol_WindowBg), th * 0.5f + 1.0f, 0, 2.0f);
    drawGrab(std::round(tr.x0 + len * std::clamp(v, 0.0f, 1.0f)), tr.cy, tr.hot);
    return changed;
}

bool numberField(const char* id, float& v, float min, float max, const char* format, float width, const char* prefix)
{
    if (width <= 0.0f) width = metrics().fieldWidth;
    NumberFont font;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImGuiID key = ImGui::GetID(id);
    char plain[32];   // typed without the format's decorations
    std::snprintf(plain, sizeof plain, "%g", std::round(v * 1000.0f) / 1000.0f);
    takeFocusRequest(key, plain);

    bool applied = false;
    if (editBox(key, p, width, ImGuiInputTextFlags_CharsScientific, [&](const char* typed) {
            float x = 0.0f;
            if (std::sscanf(typed, "%f", &x) == 1) { v = std::clamp(x, min, max); applied = true; }
        }))
        return applied;

    bool clicked = false, hot = false;
    const ImRect box = boxItem(id, width, clicked, hot);
    drawBox(box, hot);
    ImRect inner = box;
    if (prefix) {
        const float pad = ImGui::GetStyle().FramePadding.x * 0.75f;
        ImGui::GetWindowDrawList()->AddText(ImVec2(box.Min.x + pad, std::round(box.GetCenter().y - ImGui::GetFontSize() * 0.5f)),
                                            ImGui::GetColorU32(ImGuiCol_TextDisabled), prefix);
        inner.Min.x += pad + ImGui::CalcTextSize(prefix).x;
    }
    char text[32];
    std::snprintf(text, sizeof text, format, v);
    textCentered(inner, text, nullptr, ImGui::GetColorU32(ImGuiCol_Text));
    if (clicked) beginEdit(key, plain);
    return false;
}

bool hexField(const char* id, float rgb[3], float width)
{
    NumberFont font;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImGuiID key = ImGui::GetID(id);
    char hex[8];
    std::snprintf(hex, sizeof hex, "%02X%02X%02X", int(rgb[0] * 255.0f + 0.5f), int(rgb[1] * 255.0f + 0.5f), int(rgb[2] * 255.0f + 0.5f));
    takeFocusRequest(key, hex);

    bool applied = false;
    if (editBox(key, p, width, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase, [&](const char* typed) {
            unsigned x = 0;
            if (std::strlen(typed) == 6 && std::sscanf(typed, "%06x", &x) == 1) {
                rgb[0] = float((x >> 16) & 0xFF) / 255.0f;
                rgb[1] = float((x >> 8) & 0xFF) / 255.0f;
                rgb[2] = float(x & 0xFF) / 255.0f;
                applied = true;
            }
        }))
        return applied;

    bool clicked = false, hot = false;
    const ImRect box = boxItem(id, width, clicked, hot);
    drawBox(box, hot);
    char shown[12];
    std::snprintf(shown, sizeof shown, "#%s", hex);
    textCentered(box, shown, nullptr, ImGui::GetColorU32(ImGuiCol_Text));
    if (clicked) beginEdit(key, hex);
    return false;
}

bool textField(const char* id, char* buf, size_t size, float width)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImGuiID key = ImGui::GetID(id);
    takeFocusRequest(key, buf);

    bool applied = false;
    if (editBox(key, p, width, 0, [&](const char* typed) {
            if (!*typed) return;
            std::snprintf(buf, size, "%s", typed);
            applied = true;
        }))
        return applied;

    bool clicked = false, hot = false;
    const ImRect box = boxItem(id, width, clicked, hot);
    drawBox(box, hot);
    const ImVec2 at(box.Min.x + ImGui::GetStyle().FramePadding.x, std::round(box.GetCenter().y - ImGui::GetFontSize() * 0.5f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(box.Min, box.Max, true);
    dl->AddText(at, ImGui::GetColorU32(ImGuiCol_Text), buf);
    dl->PopClipRect();
    if (clicked) beginEdit(key, buf);
    return false;
}

bool searchField(const char* id, const char* hint, char* buf, size_t size, float width)
{
    const Metrics m = metrics();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, (m.boxHeight - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, m.rounding);
    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::InputTextWithHint(id, hint, buf, size);
    ImGui::PopStyleVar(2);
    return changed;
}

void focusField(const char* id)
{
    g_focusRequest = ImGui::GetID(id);
}

bool isEditing(const char* id)
{
    return g_edit.id != 0 && g_edit.id == ImGui::GetID(id);
}

bool radio(const char* label, bool active)
{
    const float d = std::round(ImGui::GetFontSize() * 0.8f);
    bool pressed = false, hovered = false;
    const ImVec2 c = toggleItem(label, d, pressed, hovered);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = d * 0.5f;
    dl->AddCircleFilled(c, r, ImGui::GetColorU32(ImGuiCol_FrameBg));
    dl->AddCircle(c, r - 0.5f, active ? accent() : hovered ? kToggleHot : kToggleEdge, 0, 1.0f);
    if (active) dl->AddCircleFilled(c, r * 0.45f, accent());
    return pressed;
}

bool checkbox(const char* label, bool& v)
{
    const float d = std::round(ImGui::GetFontSize() * 0.8f);
    bool pressed = false, hovered = false;
    const ImVec2 c = toggleItem(label, d, pressed, hovered);
    if (pressed) v = !v;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = d * 0.5f, round = 2.0f * metrics().unit;
    const ImVec2 a(c.x - r, c.y - r), b(c.x + r, c.y + r);
    if (v) {
        dl->AddRectFilled(a, b, accent(), round);
        dl->PathLineTo(ImVec2(c.x - r * 0.5f, c.y + r * 0.02f));
        dl->PathLineTo(ImVec2(c.x - r * 0.12f, c.y + r * 0.42f));
        dl->PathLineTo(ImVec2(c.x + r * 0.55f, c.y - r * 0.4f));
        dl->PathStroke(IM_COL32(20, 20, 20, 255), 0, std::max(1.5f, d * 0.12f));
    } else {
        dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBg), round);
        dl->AddRect(a, b, hovered ? kToggleHot : kToggleEdge, round, 0, 1.0f);
    }
    return pressed;
}

// ---------------------------------------------------------------- rows

bool sliderRow(const char* label, float& v, float min, float max, const char* format, bool logarithmic, float defaultValue)
{
    const Metrics m = metrics();
    ImGui::PushID(label);
    rowLabel(label);
    bool changed = slider("##slider", v, min, max, stretchWidth(m.fieldWidth), logarithmic, defaultValue);
    alignRight(m.fieldWidth);
    changed |= numberField("##field", v, min, max, format);
    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------- menus

void actionMenuItem(ActionRegistry& actions, std::string_view id)
{
    const Action* a = actions.find(id);
    if (!a) return;
    const std::string shortcut = a->shortcuts.empty() ? std::string() : shortcutLabel(a->shortcuts.front());
    const bool checked = a->checked && a->checked();
    if (ImGui::MenuItem(a->title.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), checked, a->isEnabled()))
        actions.run(id);
}

}

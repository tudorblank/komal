#include "ui/widgets/detail.hpp"
#include "ui/theme.hpp"

#include <cstdio>
#include <cstring>

using namespace Widgets::detail;

namespace Widgets {

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

}

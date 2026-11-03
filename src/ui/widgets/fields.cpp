#include "ui/widgets/interaction.hpp"
#include "base/color.hpp"
#include "ui/style/fonts.hpp"

#include <cstring>

using namespace Widgets::detail;

namespace Widgets {

bool numberField(const char* id, float& v, float min, float max, const char* format, float width, const char* prefix)
{
    const Style::Styles& s = Style::get();
    if (width <= 0.0f) width = s.metrics.fieldWidth;
    Fonts::Numbers font;
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
    char text[32];
    std::snprintf(text, sizeof text, format, v);
    Skin::valueBox(Painter::window(), s.box, box.Min, box.Max, hot, text, prefix);
    if (clicked) beginEdit(key, plain);
    return false;
}

bool hexField(const char* id, float rgb[3], float width)
{
    const Style::Styles& s = Style::get();
    Fonts::Numbers font;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImGuiID key = ImGui::GetID(id);
    char hex[8];
    std::snprintf(hex, sizeof hex, "%02X%02X%02X", unitToByte(rgb[0]), unitToByte(rgb[1]), unitToByte(rgb[2]));
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
    char shown[12];
    std::snprintf(shown, sizeof shown, "#%s", hex);
    Skin::valueBox(Painter::window(), s.box, box.Min, box.Max, hot, shown);
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
    Skin::textBox(Painter::window(), Style::get().box, box.Min, box.Max, hot, buf);
    if (clicked) beginEdit(key, buf);
    return false;
}

bool searchField(const char* id, const char* hint, char* buf, size_t size, float width)
{
    boxShapedInput(width);
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

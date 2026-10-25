#include "theme.h"
#include <imgui.h>

extern const unsigned char kFontUi[];       // generated, see embed_file() in CMakeLists.txt
extern const unsigned int  kFontUiSize;

void loadUiFonts()
{
    // A real vector font: the default ProggyClean is a pixel font, which turns into blocky
    // squares as soon as it is drawn bigger than 13px (zoomed graph, HiDPI).
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;       // static data, the atlas must not free it
    ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)kFontUi, (int)kFontUiSize, 0.0f, &cfg);
}

void applyUiStyle(float scale)
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);

    s.FontSizeBase      = 15.0f;
    s.WindowPadding     = ImVec2(10, 8);
    s.FramePadding      = ImVec2(8, 4);
    s.ItemSpacing       = ImVec2(8, 6);
    s.WindowRounding    = 4.0f;
    s.ChildRounding     = 4.0f;
    s.FrameRounding     = 4.0f;
    s.PopupRounding     = 4.0f;
    s.GrabRounding      = 4.0f;
    s.TabRounding       = 4.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowBorderSize  = 1.0f;
    s.PopupBorderSize   = 1.0f;

    ImVec4* c = s.Colors;
    const ImVec4 accent(0.33f, 0.55f, 0.95f, 1.00f);
    c[ImGuiCol_Text]                = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
    c[ImGuiCol_TextDisabled]        = ImVec4(0.50f, 0.50f, 0.54f, 1.00f);
    c[ImGuiCol_WindowBg]            = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    c[ImGuiCol_ChildBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_PopupBg]             = ImVec4(0.13f, 0.13f, 0.14f, 0.98f);
    c[ImGuiCol_Border]              = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    c[ImGuiCol_FrameBg]             = ImVec4(0.17f, 0.17f, 0.19f, 1.00f);
    c[ImGuiCol_FrameBgHovered]      = ImVec4(0.22f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_FrameBgActive]       = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);
    c[ImGuiCol_TitleBg]             = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgActive]       = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    c[ImGuiCol_MenuBarBg]           = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    c[ImGuiCol_Button]              = ImVec4(0.19f, 0.19f, 0.21f, 1.00f);
    c[ImGuiCol_ButtonHovered]       = ImVec4(0.25f, 0.25f, 0.28f, 1.00f);
    c[ImGuiCol_ButtonActive]        = ImVec4(0.30f, 0.30f, 0.34f, 1.00f);
    c[ImGuiCol_Header]              = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    c[ImGuiCol_HeaderHovered]       = ImVec4(0.25f, 0.25f, 0.29f, 1.00f);
    c[ImGuiCol_HeaderActive]        = ImVec4(0.29f, 0.29f, 0.33f, 1.00f);
    c[ImGuiCol_Separator]           = c[ImGuiCol_Border];
    c[ImGuiCol_CheckMark]           = accent;
    c[ImGuiCol_SliderGrab]          = accent;
    c[ImGuiCol_Tab]                 = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    c[ImGuiCol_TabHovered]          = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    c[ImGuiCol_TabSelected]         = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed]           = c[ImGuiCol_Tab];
    c[ImGuiCol_TabDimmedSelected]   = c[ImGuiCol_TabSelected];
    c[ImGuiCol_DockingPreview]      = ImVec4(accent.x, accent.y, accent.z, 0.50f);
    c[ImGuiCol_DockingEmptyBg]      = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
    c[ImGuiCol_NavCursor]           = accent;

    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

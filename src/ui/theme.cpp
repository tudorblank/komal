#include "theme.hpp"
#include "base/palette.hpp"
#include <imgui.h>

extern const unsigned char kFontUi[];        // generated, see embed_file() in CMakeLists.txt
extern const unsigned int  kFontUiSize;
extern const unsigned char kFontNumbers[];
extern const unsigned int  kFontNumbersSize;

namespace {
ImFont* g_numberFont      = nullptr;
ImFont* g_graphFont       = nullptr;
ImFont* g_graphNumberFont = nullptr;
}

void loadUiFonts()
{
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;       // static data, the atlas must not free it
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->AddFontFromMemoryTTF((void*)kFontUi, (int)kFontUiSize, 0.0f, &cfg);
    g_numberFont = atlas->AddFontFromMemoryTTF((void*)kFontNumbers, (int)kFontNumbersSize, 0.0f, &cfg);

    ImFontConfig graph = cfg;
    graph.RasterizerMultiply = 1.4f;   // glyph coverage brightened: reads as a heavier weight
    g_graphFont       = atlas->AddFontFromMemoryTTF((void*)kFontUi, (int)kFontUiSize, 0.0f, &graph);
    g_graphNumberFont = atlas->AddFontFromMemoryTTF((void*)kFontNumbers, (int)kFontNumbersSize, 0.0f, &graph);
}

ImFont* numberFont()      { return g_numberFont; }
ImFont* graphFont()       { return g_graphFont; }
ImFont* graphNumberFont() { return g_graphNumberFont; }

NumberFont::NumberFont()  { ImGui::PushFont(g_numberFont, 0.0f); }   // 0: keep the current size
NumberFont::~NumberFont() { ImGui::PopFont(); }

void applyUiStyle(float scale)
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();

    s.FontSizeBase      = 15.0f;
    s.WindowPadding     = ImVec2(10, 8);
    s.FramePadding      = ImVec2(8, 4);
    s.ItemSpacing       = ImVec2(8, 6);
    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 6.0f;
    s.FrameRounding     = 5.0f;
    s.PopupRounding     = 6.0f;
    s.GrabRounding      = 5.0f;
    s.TabRounding       = 5.0f;
    s.ScrollbarRounding = 8.0f;
    s.ScrollbarSize     = 12.0f;
    s.WindowBorderSize  = 1.0f;
    s.PopupBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;

    const auto grey = [](float v, float a = 1.0f) { return ImVec4(v, v, v, a); };
    const auto [ar, ag, ab, aa] = Palette::kAccent.floats();
    const ImVec4 accent(ar, ag, ab, aa);
    const auto tint = [&](float a) { return ImVec4(accent.x, accent.y, accent.z, a); };

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                      = grey(0.86f);
    c[ImGuiCol_TextDisabled]              = grey(0.46f);
    c[ImGuiCol_WindowBg]                  = grey(0.086f);
    c[ImGuiCol_ChildBg]                   = grey(0.0f, 0.0f);
    c[ImGuiCol_PopupBg]                   = grey(0.106f, 0.98f);
    c[ImGuiCol_Border]                    = grey(0.17f, 0.80f);
    c[ImGuiCol_BorderShadow]              = grey(0.0f, 0.0f);
    c[ImGuiCol_FrameBg]                   = grey(0.13f);
    c[ImGuiCol_FrameBgHovered]            = grey(0.16f);
    c[ImGuiCol_FrameBgActive]             = grey(0.19f);
    c[ImGuiCol_TitleBg]                   = grey(0.067f);
    c[ImGuiCol_TitleBgActive]             = grey(0.067f);
    c[ImGuiCol_TitleBgCollapsed]          = grey(0.067f);
    c[ImGuiCol_MenuBarBg]                 = grey(0.067f);
    c[ImGuiCol_ScrollbarBg]               = grey(0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab]             = grey(0.22f);
    c[ImGuiCol_ScrollbarGrabHovered]      = grey(0.27f);
    c[ImGuiCol_ScrollbarGrabActive]       = grey(0.32f);
    c[ImGuiCol_CheckMark]                 = accent;
    c[ImGuiCol_SliderGrab]                = tint(0.85f);
    c[ImGuiCol_SliderGrabActive]          = accent;
    c[ImGuiCol_Button]                    = grey(0.14f);
    c[ImGuiCol_ButtonHovered]             = grey(0.18f);
    c[ImGuiCol_ButtonActive]              = grey(0.22f);
    c[ImGuiCol_Header]                    = grey(0.15f);
    c[ImGuiCol_HeaderHovered]             = grey(0.19f);
    c[ImGuiCol_HeaderActive]              = grey(0.22f);
    c[ImGuiCol_Separator]                 = c[ImGuiCol_Border];
    c[ImGuiCol_SeparatorHovered]          = tint(0.60f);
    c[ImGuiCol_SeparatorActive]           = accent;
    c[ImGuiCol_ResizeGrip]                = grey(0.30f, 0.20f);
    c[ImGuiCol_ResizeGripHovered]         = tint(0.60f);
    c[ImGuiCol_ResizeGripActive]          = accent;
    c[ImGuiCol_InputTextCursor]           = c[ImGuiCol_Text];
    c[ImGuiCol_Tab]                       = grey(0.067f);
    c[ImGuiCol_TabHovered]                = grey(0.15f);
    c[ImGuiCol_TabSelected]               = grey(0.086f);
    c[ImGuiCol_TabSelectedOverline]       = accent;
    c[ImGuiCol_TabDimmed]                 = c[ImGuiCol_Tab];
    c[ImGuiCol_TabDimmedSelected]         = c[ImGuiCol_TabSelected];
    c[ImGuiCol_TabDimmedSelectedOverline] = grey(0.0f, 0.0f);
    c[ImGuiCol_DockingPreview]            = tint(0.40f);
    c[ImGuiCol_DockingEmptyBg]            = grey(0.055f);
    c[ImGuiCol_PlotLines]                 = grey(0.60f);
    c[ImGuiCol_PlotLinesHovered]          = accent;
    c[ImGuiCol_PlotHistogram]             = tint(0.80f);
    c[ImGuiCol_PlotHistogramHovered]      = accent;
    c[ImGuiCol_TableHeaderBg]             = grey(0.12f);
    c[ImGuiCol_TableBorderStrong]         = grey(0.17f);
    c[ImGuiCol_TableBorderLight]          = grey(0.13f);
    c[ImGuiCol_TableRowBg]                = grey(0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt]             = grey(1.0f, 0.02f);
    c[ImGuiCol_TextLink]                  = accent;
    c[ImGuiCol_TextSelectedBg]            = tint(0.30f);
    c[ImGuiCol_TreeLines]                 = c[ImGuiCol_Border];
    c[ImGuiCol_DragDropTarget]            = accent;
    c[ImGuiCol_DragDropTargetBg]          = tint(0.12f);
    c[ImGuiCol_UnsavedMarker]             = c[ImGuiCol_Text];
    c[ImGuiCol_NavCursor]                 = accent;
    c[ImGuiCol_NavWindowingHighlight]     = grey(1.0f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]         = grey(0.0f, 0.40f);
    c[ImGuiCol_ModalWindowDimBg]          = grey(0.0f, 0.50f);

    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}
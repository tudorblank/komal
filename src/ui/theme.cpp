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
    atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontUi), int(kFontUiSize), 0.0f, &cfg);
    g_numberFont = atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontNumbers), int(kFontNumbersSize), 0.0f, &cfg);

    ImFontConfig graph = cfg;
    graph.RasterizerMultiply = 1.4f;   // glyph coverage brightened: reads as a heavier weight
    g_graphFont       = atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontUi), int(kFontUiSize), 0.0f, &graph);
    g_graphNumberFont = atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontNumbers), int(kFontNumbersSize), 0.0f, &graph);
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

    s.FontSizeBase      = kBaseFontSize;
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

    const auto col = [](Color8 c) {
        const auto [r, g, b, a] = c.floats();
        return ImVec4(r, g, b, a);
    };
    const ImVec4 accent = col(Palette::kAccent);
    const auto tint = [&](float a) { return ImVec4(accent.x, accent.y, accent.z, a); };

    using namespace Palette;
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                      = col(kText);
    c[ImGuiCol_TextDisabled]              = col(kTextDim);
    c[ImGuiCol_WindowBg]                  = col(kPanel);
    c[ImGuiCol_ChildBg]                   = col(kNone);
    c[ImGuiCol_PopupBg]                   = col(kPopup);
    c[ImGuiCol_Border]                    = col(kBorder);
    c[ImGuiCol_BorderShadow]              = col(kNone);
    c[ImGuiCol_FrameBg]                   = col(kField);
    c[ImGuiCol_FrameBgHovered]            = col(kFieldHot);
    c[ImGuiCol_FrameBgActive]             = col(kFieldActive);
    c[ImGuiCol_TitleBg]                   = col(kBar);
    c[ImGuiCol_TitleBgActive]             = col(kBar);
    c[ImGuiCol_TitleBgCollapsed]          = col(kBar);
    c[ImGuiCol_MenuBarBg]                 = col(kBar);
    c[ImGuiCol_ScrollbarBg]               = col(kNone);
    c[ImGuiCol_ScrollbarGrab]             = col(kScrollGrab);
    c[ImGuiCol_ScrollbarGrabHovered]      = col(kScrollGrabHot);
    c[ImGuiCol_ScrollbarGrabActive]       = col(kScrollGrabActive);
    c[ImGuiCol_CheckMark]                 = accent;
    c[ImGuiCol_SliderGrab]                = tint(0.85f);
    c[ImGuiCol_SliderGrabActive]          = accent;
    c[ImGuiCol_Button]                    = col(kButton);
    c[ImGuiCol_ButtonHovered]             = col(kButtonHot);
    c[ImGuiCol_ButtonActive]              = col(kButtonActive);
    c[ImGuiCol_Header]                    = col(kRow);
    c[ImGuiCol_HeaderHovered]             = col(kRowHot);
    c[ImGuiCol_HeaderActive]              = col(kRowActive);
    c[ImGuiCol_Separator]                 = c[ImGuiCol_Border];
    c[ImGuiCol_SeparatorHovered]          = tint(0.60f);
    c[ImGuiCol_SeparatorActive]           = accent;
    c[ImGuiCol_ResizeGrip]                = col(kResizeGrip);
    c[ImGuiCol_ResizeGripHovered]         = tint(0.60f);
    c[ImGuiCol_ResizeGripActive]          = accent;
    c[ImGuiCol_InputTextCursor]           = c[ImGuiCol_Text];
    c[ImGuiCol_Tab]                       = col(kBar);
    c[ImGuiCol_TabHovered]                = col(kRow);
    c[ImGuiCol_TabSelected]               = col(kPanel);
    c[ImGuiCol_TabSelectedOverline]       = accent;
    c[ImGuiCol_TabDimmed]                 = c[ImGuiCol_Tab];
    c[ImGuiCol_TabDimmedSelected]         = c[ImGuiCol_TabSelected];
    c[ImGuiCol_TabDimmedSelectedOverline] = col(kNone);
    c[ImGuiCol_DockingPreview]            = tint(0.40f);
    c[ImGuiCol_DockingEmptyBg]            = col(kWindowBackground);
    c[ImGuiCol_PlotLines]                 = col(kPlot);
    c[ImGuiCol_PlotLinesHovered]          = accent;
    c[ImGuiCol_PlotHistogram]             = tint(0.80f);
    c[ImGuiCol_PlotHistogramHovered]      = accent;
    c[ImGuiCol_TableHeaderBg]             = col(kTableHeader);
    c[ImGuiCol_TableBorderStrong]         = col(kTableBorder);
    c[ImGuiCol_TableBorderLight]          = col(kTableLine);
    c[ImGuiCol_TableRowBg]                = col(kNone);
    c[ImGuiCol_TableRowBgAlt]             = col(kRowAlt);
    c[ImGuiCol_TextLink]                  = accent;
    c[ImGuiCol_TextSelectedBg]            = tint(0.30f);
    c[ImGuiCol_TreeLines]                 = c[ImGuiCol_Border];
    c[ImGuiCol_DragDropTarget]            = accent;
    c[ImGuiCol_DragDropTargetBg]          = tint(0.12f);
    c[ImGuiCol_UnsavedMarker]             = c[ImGuiCol_Text];
    c[ImGuiCol_NavCursor]                 = accent;
    c[ImGuiCol_NavWindowingHighlight]     = col(kNavHighlight);
    c[ImGuiCol_NavWindowingDimBg]         = col(kNavDim);
    c[ImGuiCol_ModalWindowDimBg]          = col(kModalDim);

    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}
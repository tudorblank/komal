#include "ui/style/ui-style.hpp"
#include "ui/style/palette.hpp"

#include <algorithm>
#include <cmath>

namespace Style {

namespace {

float g_appliedScale = 0.0f;   // apply()'s last: a theme change styles ImGui again at it

ImVec4 floats(Color8 c)
{
    const auto [r, g, b, a] = c.floats();
    return ImVec4(r, g, b, a);
}

ImU32 packed(Color8 c) { return c.packed(); }

Metrics measure()
{
    const ImGuiStyle& st = ImGui::GetStyle();
    const float font = ImGui::GetFontSize();
    const float u = font / kBaseFontSize;
    Metrics m{};
    m.font            = font;
    m.unit            = u;
    m.rowHeight       = ImGui::GetFrameHeight();
    m.boxHeight       = std::round(font + 6.0f * u);
    m.fieldWidth      = std::round(font * 3.6f);
    m.labelWidth      = std::round(font * 5.0f);
    m.shortLabelWidth = std::round(font * 1.1f);
    m.spacing         = st.ItemSpacing.x;
    m.groupSpacing    = std::round(font);
    m.innerSpacing    = std::round(st.ItemInnerSpacing.x);
    m.framePadding    = st.FramePadding.x;
    m.rounding        = std::round(3.0f * u);
    m.frameRounding   = st.FrameRounding;
    m.panelPadding    = std::round(10.0f * u);
    m.sectionGap      = std::round(6.0f * u);
    return m;
}

Styles build(const Metrics& m)
{
    const Palette& pal = Palette::current();
    const float u = m.unit, font = m.font;
    Styles s{};
    s.metrics = m;
    s.text = { packed(pal.text), packed(pal.textDim) };
    s.box  = { packed(pal.field), packed(pal.fieldHot), packed(pal.text), packed(pal.textDim), m.rounding, m.framePadding };
    s.icon = { packed(pal.text), 0.22f, 0.08f, 1.25f };
    s.swatch = { packed(pal.darkRim), packed(pal.toggleHot), m.rounding };

    s.toggle.mark          = std::round(font * 0.8f);
    s.toggle.fill          = packed(pal.field);
    s.toggle.edge          = packed(pal.toggleEdge);
    s.toggle.edgeHot       = packed(pal.toggleHot);
    s.toggle.on            = packed(pal.accent);
    s.toggle.check         = packed(pal.checkMark);
    s.toggle.checkRounding = 2.0f * u;
    s.toggle.checkStroke   = std::max(1.5f, s.toggle.mark * 0.12f);
    s.toggle.dot           = 0.45f;
    s.toggle.label         = packed(pal.text);

    s.slider = slider(u, Tone::Panel);

    s.figureSlider.grab = s.slider;
    s.figureSlider.grab.grabHalfHeight = std::round(m.boxHeight * 0.38f);
    s.figureSlider.margin    = s.slider.reach() + std::round(2.0f * u);
    s.figureSlider.boxHeight = std::round(m.boxHeight * 0.62f) + 1.0f;
    s.figureSlider.padding   = std::round(2.0f * u);
    s.figureSlider.sliver    = std::max(1.0f, std::round(u));

    s.gradientSlider = { s.slider, std::round(8.0f * u), packed(pal.panel), 2.0f };

    s.toolButton.size         = std::round(font * 1.75f);
    s.toolButton.padding      = std::round(font * 0.25f);
    s.toolButton.bar          = packed(pal.bar);
    s.toolButton.fillHot      = packed(pal.buttonHot);
    s.toolButton.fillSelected = packed(pal.buttonActive);
    s.toolButton.outline      = packed(pal.accent);
    s.toolButton.icon         = packed(pal.text);
    s.toolButton.iconDim      = packed(pal.textDim);
    s.toolButton.rounding     = m.frameRounding;
    s.toolButton.iconSize     = 0.62f;

    s.notice = { font * 0.6f, font * 0.4f, font * 28.0f, font * 0.3f, font * 0.2f, packed(pal.noticeBack), packed(pal.noticeMark), packed(pal.hudText) };
    s.hud    = { ImVec2(8.0f, 6.0f), ImVec2(4.0f, 2.0f), 3.0f, packed(pal.hudBack), packed(pal.hudText) };

    s.history.rowHeight     = std::round(font * 1.55f);
    s.history.laneWidth     = std::round(font * 0.95f);
    s.history.dot           = std::max(3.0f, std::round(font * 0.24f));
    s.history.line          = std::max(1.0f, std::round(font * 0.1f));
    s.history.ring          = s.history.dot + s.history.line * 2.5f;
    s.history.labelGap      = std::round(font * 0.5f);
    s.history.labelRoom     = font * 12.0f;
    s.history.numberPadding = font * 0.4f;
    s.history.rounding      = m.frameRounding;
    s.history.here          = packed(pal.accent);
    s.history.ahead         = packed(pal.accent.withAlpha(unitToByte(0.45f)));
    s.history.other         = packed(pal.textDim.withAlpha(unitToByte(0.7f)));
    s.history.text          = packed(pal.text);
    s.history.textDim       = packed(pal.textDim);
    s.history.rowHot        = packed(pal.rowHot);
    s.history.hollow        = packed(pal.panel);

    s.map = { 6.0f * u, 60.0f, m.frameRounding, packed(pal.bar), packed(pal.emptyArea), packed(pal.viewOutline), packed(pal.textDim), 1.5f, 2.5f * u, 2.0f };

    ColorPickerStyle& c = s.colorPicker;
    c.block          = std::round(font * 2.6f);
    c.swatchShare    = 0.64f;
    c.swatchRounding = 3.0f;
    c.swatchGap      = 2.0f;
    c.rim            = packed(pal.darkRim);
    c.lightRim       = packed(pal.lightRim);
    c.gap            = packed(pal.panel);
    c.edited         = packed(pal.accent);
    c.editedWidth    = 1.5f;
    c.swap           = packed(pal.textDim);
    c.swapHot        = packed(pal.text);
    c.swapStroke     = 1.5f;
    c.swapHeadMin    = 2.5f;
    c.swapHead       = 0.16f;
    c.ringSegments   = 96;
    c.ringInner      = 0.80f;
    c.square         = 0.70f;
    c.ringEdge       = packed(pal.panel);
    c.ringEdgeWidth  = 1.5f;
    c.white          = packed(pal.white);
    c.black          = packed(pal.black);
    c.clear          = packed(pal.clear);
    c.markerRim      = packed(pal.darkRim);
    c.marker         = packed(pal.marker);
    c.markerRimWidth = 2.0f;
    c.markerWidth    = 1.5f;
    c.hueMarker       = 0.30f;
    c.squareMarker    = 0.8f;
    c.squareMarkerMin = 4.0f;
    c.maxSize        = 320.0f * u;
    c.minSize        = 80.0f;

    s.menu = { font * 10.0f, font * 12.0f, font * 7.0f };
    return s;
}

}

const std::vector<std::string_view>& themes() { return Palette::themes(); }
bool useTheme(std::string_view name, std::string& why)
{
    if (!Palette::useTheme(name, why)) return false;
    if (g_appliedScale > 0.0f) apply(g_appliedScale);   // ImGui's colours too, once it's styled
    return true;
}

Color8 windowBackground() { return Palette::current().windowBackground; }

const CanvasStyle& canvas()
{
    static CanvasStyle style;
    static uint64_t madeFor = 0;   // the palette's revision
    if (madeFor == Palette::revision()) return style;
    madeFor = Palette::revision();
    const Palette& pal = Palette::current();
    style = { pal.canvasBackground, pal.canvasCheckerA, pal.canvasCheckerB, pal.canvasChecker, pal.pictureCheckerA, pal.pictureCheckerB,
              pal.pictureChecker, pal.originMark, pal.accent, 1.0f, pal.faintOutline, pal.accent, 1.5f };
    return style;
}

SliderStyle slider(float unit, Tone tone)
{
    const Palette& pal = Palette::current();
    SliderStyle s{};
    s.track          = packed(tone == Tone::Node ? pal.nodeTrack : pal.track);
    s.fill           = packed(pal.accentDim);
    s.grab           = packed(pal.grab);
    s.grabHot        = packed(pal.grabHot);
    s.rim            = packed(pal.grabRim);
    s.trackThickness = std::max(2.0f, std::round(3.0f * unit));
    s.grabHalfWidth  = std::max(1.0f, std::round(3.0f * unit));
    s.grabHalfHeight = std::max(2.0f, std::round(6.5f * unit));
    s.rimWidth       = 1.0f;   // pixels, at any scale
    s.grabRounding   = 1.5f * unit;
    s.rimRounding    = 2.0f * unit;
    return s;
}

const Styles& get()
{
    // Made again only when what it comes from changes: the font size, the style and the palette.
    static Styles styles;
    static float madeFor[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
    static uint64_t paletteFor = 0;
    const ImGuiStyle& st = ImGui::GetStyle();
    const float key[4] = { ImGui::GetFontSize(), ImGui::GetFrameHeight(), st.ItemSpacing.x, st.FrameRounding };
    if (std::equal(std::begin(key), std::end(key), std::begin(madeFor)) && paletteFor == Palette::revision()) return styles;
    std::copy(std::begin(key), std::end(key), std::begin(madeFor));
    paletteFor = Palette::revision();
    styles = build(measure());
    return styles;
}

void apply(float scale)
{
    g_appliedScale = scale;
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

    const Palette& pal = Palette::current();
    const ImVec4 accent = floats(pal.accent);
    const auto tint = [&](float a) { return ImVec4(accent.x, accent.y, accent.z, a); };

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                      = floats(pal.text);
    c[ImGuiCol_TextDisabled]              = floats(pal.textDim);
    c[ImGuiCol_WindowBg]                  = floats(pal.panel);
    c[ImGuiCol_ChildBg]                   = floats(pal.none);
    c[ImGuiCol_PopupBg]                   = floats(pal.popup);
    c[ImGuiCol_Border]                    = floats(pal.border);
    c[ImGuiCol_BorderShadow]              = floats(pal.none);
    c[ImGuiCol_FrameBg]                   = floats(pal.field);
    c[ImGuiCol_FrameBgHovered]            = floats(pal.fieldHot);
    c[ImGuiCol_FrameBgActive]             = floats(pal.fieldActive);
    c[ImGuiCol_TitleBg]                   = floats(pal.bar);
    c[ImGuiCol_TitleBgActive]             = floats(pal.bar);
    c[ImGuiCol_TitleBgCollapsed]          = floats(pal.bar);
    c[ImGuiCol_MenuBarBg]                 = floats(pal.bar);
    c[ImGuiCol_ScrollbarBg]               = floats(pal.none);
    c[ImGuiCol_ScrollbarGrab]             = floats(pal.scrollGrab);
    c[ImGuiCol_ScrollbarGrabHovered]      = floats(pal.scrollGrabHot);
    c[ImGuiCol_ScrollbarGrabActive]       = floats(pal.scrollGrabActive);
    c[ImGuiCol_CheckMark]                 = accent;
    c[ImGuiCol_SliderGrab]                = tint(0.85f);
    c[ImGuiCol_SliderGrabActive]          = accent;
    c[ImGuiCol_Button]                    = floats(pal.button);
    c[ImGuiCol_ButtonHovered]             = floats(pal.buttonHot);
    c[ImGuiCol_ButtonActive]              = floats(pal.buttonActive);
    c[ImGuiCol_Header]                    = floats(pal.row);
    c[ImGuiCol_HeaderHovered]             = floats(pal.rowHot);
    c[ImGuiCol_HeaderActive]              = floats(pal.rowActive);
    c[ImGuiCol_Separator]                 = c[ImGuiCol_Border];
    c[ImGuiCol_SeparatorHovered]          = tint(0.60f);
    c[ImGuiCol_SeparatorActive]           = accent;
    c[ImGuiCol_ResizeGrip]                = floats(pal.resizeGrip);
    c[ImGuiCol_ResizeGripHovered]         = tint(0.60f);
    c[ImGuiCol_ResizeGripActive]          = accent;
    c[ImGuiCol_InputTextCursor]           = c[ImGuiCol_Text];
    c[ImGuiCol_Tab]                       = floats(pal.bar);
    c[ImGuiCol_TabHovered]                = floats(pal.row);
    c[ImGuiCol_TabSelected]               = floats(pal.panel);
    c[ImGuiCol_TabSelectedOverline]       = accent;
    c[ImGuiCol_TabDimmed]                 = c[ImGuiCol_Tab];
    c[ImGuiCol_TabDimmedSelected]         = c[ImGuiCol_TabSelected];
    c[ImGuiCol_TabDimmedSelectedOverline] = floats(pal.none);
    c[ImGuiCol_DockingPreview]            = tint(0.40f);
    c[ImGuiCol_DockingEmptyBg]            = floats(pal.windowBackground);
    c[ImGuiCol_PlotLines]                 = floats(pal.plot);
    c[ImGuiCol_PlotLinesHovered]          = accent;
    c[ImGuiCol_PlotHistogram]             = tint(0.80f);
    c[ImGuiCol_PlotHistogramHovered]      = accent;
    c[ImGuiCol_TableHeaderBg]             = floats(pal.tableHeader);
    c[ImGuiCol_TableBorderStrong]         = floats(pal.tableBorder);
    c[ImGuiCol_TableBorderLight]          = floats(pal.tableLine);
    c[ImGuiCol_TableRowBg]                = floats(pal.none);
    c[ImGuiCol_TableRowBgAlt]             = floats(pal.rowAlt);
    c[ImGuiCol_TextLink]                  = accent;
    c[ImGuiCol_TextSelectedBg]            = tint(0.30f);
    c[ImGuiCol_TreeLines]                 = c[ImGuiCol_Border];
    c[ImGuiCol_DragDropTarget]            = accent;
    c[ImGuiCol_DragDropTargetBg]          = tint(0.12f);
    c[ImGuiCol_UnsavedMarker]             = c[ImGuiCol_Text];
    c[ImGuiCol_NavCursor]                 = accent;
    c[ImGuiCol_NavWindowingHighlight]     = floats(pal.navHighlight);
    c[ImGuiCol_NavWindowingDimBg]         = floats(pal.navDim);
    c[ImGuiCol_ModalWindowDimBg]          = floats(pal.modalDim);

    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
}

}

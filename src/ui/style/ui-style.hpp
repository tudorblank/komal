#pragma once
// The UI's look, defined in one place: the palette's colours (palette.hpp, made from a theme
// file) and the UI's sizes, made into one style per component. ImGui's own widgets (menus, tabs, popups, scrollbars) get
// the same colours through apply(). Nothing else in the UI holds a colour or a look's size: a
// control borrows its style from here and hands it to its skin (ui/render/skins.hpp), which
// draws it.
//
//   Style::apply(contentScale);              // at startup, and when the window's scale changes
//   const Style::Styles& s = Style::get();   // every component, at the panels' scale
//   Skin::slider(painter, s.slider, ...);
//   Skin::slider(painter, Style::slider(zoom, Style::Tone::Node), ...);   // at another scale: a node's
//
// Sizes come in UI units: one unit is a pixel at scale 1 for the base font (kBaseFontSize), and
// grows with the font and the window's content scale.

#include "base/color.hpp"

#include <imgui.h>
#include <string>
#include <string_view>
#include <vector>

namespace Style {

constexpr float kBaseFontSize = 15.0f;   // the UI font's size at scale 1, points

// The theme the palette is made from (assets/themes/<name>.toml), any time: everything made from
// the palette follows (ImGui's colours, every style, the canvas). False, and why, if it can't be
// read; the one in use stays.
const std::vector<std::string_view>& themes();   // the built-in ones
bool useTheme(std::string_view name, std::string& why);

// ImGui's style from the palette, scaled by the window's content scale.
void apply(float contentScale);
Color8 windowBackground();   // the window, where no panel is: what each frame is cleared to

// ---- the layout every panel follows (ui/widgets): rows, boxes, columns
struct Metrics {
    float font;              // the current font size
    float unit;              // UI scale: font / kBaseFontSize
    float rowHeight;         // every row
    float boxHeight;         // fields, buttons, swatches
    float fieldWidth;        // value fields
    float labelWidth;        // the label column of property rows
    float shortLabelWidth;   // ... for one-letter labels (H, S, B)
    float spacing;           // between controls in a row
    float groupSpacing;      // between the options of a group (toggles)
    float innerSpacing;      // between a mark and its label
    float framePadding;      // inside a box, before its text
    float rounding;          // a box's corners
    float frameRounding;     // ImGui's frames' corners (a hovered row, a tool button)
    float panelPadding;      // inside a panel, every side
    float sectionGap;        // between a panel's sections
};

// ---- the controls' styles
struct TextStyle {
    ImU32 normal, dim;   // text, and disabled text or a hint
};

// The box every field, button and swatch is drawn in.
struct BoxStyle {
    ImU32 fill, fillHot;
    ImU32 text, prefix;   // its text, and what goes dim before it (a field's "R")
    float rounding;
    float padding;        // before left-aligned text
};

struct IconStyle {
    ImU32 ink;
    float size;     // of the box it's in
    float stroke;   // ... the lines' width
    float minStroke;
};

// A colour shown in a box.
struct SwatchStyle {
    ImU32 rim, rimHot;
    float rounding;
};

// Radio buttons and check boxes: a mark, then the label.
struct ToggleStyle {
    float mark;              // its diameter
    ImU32 fill, edge, edgeHot, on;
    ImU32 check;             // the tick, drawn on `on`
    float checkRounding, checkStroke;
    float dot;               // a chosen radio's dot, of the mark's radius
    ImU32 label;
};

enum class Tone : unsigned char { Panel, Node };   // a slider's track on a panel, or on a node

// A thin track filled up to the value, under a small pill grab with a dark rim.
struct SliderStyle {
    ImU32 track, fill, grab, grabHot, rim;
    float trackThickness;
    float grabHalfWidth, grabHalfHeight;
    float rimWidth;
    float grabRounding, rimRounding;
    float reach() const { return grabHalfWidth + rimWidth; }   // how far the grab reaches either side of the value
};

// A thick slider: a box with a figure in it showing how the value runs (the navigator's zoom and
// rotation), the part the value covers filled, under a box-tall grab.
struct FigureSliderStyle {
    SliderStyle grab;     // its grab (taller), and the figure's colours
    float margin;         // the grab's travel kept clear at each end
    float boxHeight;
    float padding;        // the figure inside the box
    float sliver;         // a ramp's thin end
};

// A slider whose track is a gradient (hue, saturation, brightness).
struct GradientSliderStyle {
    SliderStyle grab;
    float thickness;
    ImU32 edge;           // drawn over the gradient's corners: the panel's colour
    float edgeWidth;
    ImU32 checkerA, checkerB;   // under a gradient that's see-through (an alpha slider)
    float checkerCell;
};

// The Toolbox's square tool buttons.
struct ToolButtonStyle {
    float size, padding;
    ImU32 bar;                    // the bar's background: one band with the menu bar
    ImU32 fillHot, fillSelected, outline;
    ImU32 icon, iconDim;
    float rounding;
    float iconSize;               // of the button
};

// A message in the window's corner.
struct NoticeStyle {
    float padding, gap, maxWidth, rounding, markWidth;
    ImU32 back, mark, text;
};

// A status line over a picture.
struct HudStyle {
    ImVec2 margin;    // from the view's corner
    ImVec2 padding;   // around the text
    float  rounding;
    ImU32  back, text;
};

// The History Tree: a lane per branch, a dot per step.
struct HistoryStyle {
    float rowHeight, laneWidth, dot, line, ring;
    float labelGap, labelRoom, numberPadding, rounding;
    ImU32 here, ahead, other;     // the way back to the start, redo's way on, the rest
    ImU32 text, textDim, rowHot, hollow;
};

// A map of the picture with the view on it (the navigator).
struct MapStyle {
    float inset, minHeight, rounding;
    ImU32 back, empty, view, text;
    float viewWidth, offMapDot, offMapLine;
};

// The colour picker: the swatch pair, the hue ring and its square.
struct ColorPickerStyle {
    float block;                       // the swatch pair's corner
    float swatchShare;                 // each swatch, of the block
    float swatchRounding, swatchGap;   // a swatch's corners, and the panel colour around it
    ImU32 rim, lightRim, gap, edited;
    float editedWidth;
    ImU32 swap, swapHot;               // the swap arrow
    float swapStroke, swapHeadMin, swapHead;
    int   ringSegments;
    float ringInner, square;           // the ring's inside, of its outside; the square, of the ring's inside
    ImU32 ringEdge;
    float ringEdgeWidth;
    ImU32 white, black, clear;
    ImU32 markerRim, marker;           // a marker: a light ring inside a dark one
    float markerRimWidth, markerWidth;
    float hueMarker, squareMarker, squareMarkerMin;   // marker radii: of the ring's width, of the hue marker's
    float maxSize, minSize;
    ImU32 checkerA, checkerB;   // under a colour that's see-through
    float checkerCell;
    float floatWidth;           // the floating picker (FloatColorPick): its square and rows
};

// The graph's popups: the value being typed, the add-node menu.
struct MenuStyle {
    float valueWidth;    // the field a value is typed into
    float searchWidth;   // the add-node menu's search field
    float hintColumn;    // where its kinds' notes start
};

// The canvas engine's look: what's behind a picture and the marks drawn over it. Colours as
// Color8, the way its GPU passes take them.
struct CanvasStyle {
    Color8 background;                       // a view, before its checkerboard
    Color8 checkerA, checkerB;               // behind the canvas's picture, `A` at its origin
    float  checkerCell;                      // ... its squares, points (they keep their size on screen)
    Color8 pictureCheckerA, pictureCheckerB; // behind small pictures (navigator, thumbnails), `A` at a corner
    float  pictureCheckerCell;               // ... their squares, pixels
    Color8 origin;                           // the "+" on the canvas's 0, 0
    Color8 selection;                        // around what the selected nodes cover
    float  selectionWidth;
    Color8 previewFrom;                      // where a dragged edit's area was
    Color8 previewTo;                        // ... and where it goes
    float  previewToWidth;
    Color8 artboardFrame;                    // an artboard's rect ...
    Color8 artboardLabel;                    // ... and its name
};
const CanvasStyle& canvas();   // scale-free: any time

// Every component, at the panels' scale.
struct Styles {
    Metrics             metrics;
    TextStyle           text;
    BoxStyle            box;
    IconStyle           icon;
    SwatchStyle         swatch;
    ToggleStyle         toggle;
    SliderStyle         slider;
    FigureSliderStyle   figureSlider;
    GradientSliderStyle gradientSlider;
    ToolButtonStyle     toolButton;
    NoticeStyle         notice;
    HudStyle            hud;
    HistoryStyle        history;
    MapStyle            map;
    ColorPickerStyle    colorPicker;
    MenuStyle           menu;
};

// At the current font and ImGui style; made again only when they change. Inside a frame.
const Styles& get();
inline const Metrics& metrics() { return get().metrics; }

// A slider at any scale: `unit` UI units to the pixel (the graph's zoom).
SliderStyle slider(float unit, Tone tone);

}

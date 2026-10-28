#pragma once
// The UI's controls and the one layout standard they follow. Panels use these instead of
// ImGui's own sliders, fields, buttons and toggles, so everything matches.
//
// Tool windows open with beginPanel(): panelPadding on every side, sectionGap between sections.
// Every row is rowHeight tall; every box (field, button, swatch) is boxHeight tall, centred in
// it. A row reads left to right:
//
//   [label column] [controls, the last one stretching] [value field, flush right]
//
// Value fields are placed against the right edge (alignRight), never after summed widths, so
// they line up down a panel and across panels.

#include <imgui.h>
#include <cstddef>
#include <limits>
#include <string_view>

class ActionRegistry;

namespace Widgets {

struct Metrics {
    float unit;              // UI scale: font size / 15
    float rowHeight;         // every row
    float boxHeight;         // fields, buttons, swatches
    float fieldWidth;        // value fields
    float labelWidth;        // the label column of property rows
    float shortLabelWidth;   // ... for one-letter labels (H, S, B)
    float spacing;           // between controls in a row
    float groupSpacing;      // between the options of a group (toggles)
    float rounding;
    float panelPadding;      // inside a panel, every side
    float sectionGap;        // between a panel's sections
};
Metrics metrics();

constexpr float kNoDefault = std::numeric_limits<float>::quiet_NaN();

// ---- panels
// A tool window whose close button runs `closeAction` (its View toggle). Always pair with endPanel().
bool  beginPanel(const char* name, ActionRegistry& actions, const char* closeAction, ImGuiWindowFlags flags = 0);
void  endPanel();
void  sectionGap();
// The height `rows` control rows, `headings` heading lines and `gaps` section gaps take, spacing
// included: what a panel that fills its height (a map, a wheel) leaves for them.
float stackHeight(int rows, int headings = 0, int gaps = 0);

// ---- layout
void  heading(const char* text);                        // a label on its own line
void  rowLabel(const char* text, float width = 0.0f);   // the label column (0: labelWidth); controls follow
void  sameRow();                                        // the next control on this row, a standard gap away
void  nextGroup();                                      // the next option of a group (toggles), a wider gap away
float stretchWidth(float reservedRight = 0.0f);         // from the cursor to the right edge, less `reservedRight` and a gap
void  alignRight(float width);                          // the next control on this row ends at the right edge

// ---- controls
enum class Icon { Minus, Plus, RotateLeft, RotateRight };

bool button(const char* label, float width = 0.0f);     // 0: fits the label
bool iconButton(const char* id, Icon icon);             // a square box
bool swatch(const char* id, const float rgba[4]);       // a square box showing a colour; true when clicked

// A thin slider. True while it changes `v`; double-click sets `defaultValue` (if not kNoDefault).
bool slider(const char* id, float& v, float min, float max, float width, bool logarithmic = false, float defaultValue = kNoDefault);
// A slider whose track shows `stops` (evenly spaced colours); `v` in 0..1.
bool gradientSlider(const char* id, float& v, const ImU32* stops, int count, float width);
// A thick slider (Clip Studio's navigator): a field's box with a filled figure inside showing how
// the value runs, the part the value covers in the slider blue, under a box-tall grab. Ramp: a
// wedge growing left to right (zoom). Wave: falls and rises about the middle, filled from there
// (rotation). Otherwise as slider().
enum class SliderFigure { Ramp, Wave };
bool figureSlider(const char* id, float& v, float min, float max, float width, SliderFigure figure,
                  bool logarithmic = false, float defaultValue = kNoDefault);

// A number, centred in a box (`prefix` drawn dim before it); click to type, applied on Enter or
// when left. True then. Width 0: fieldWidth.
bool numberField(const char* id, float& v, float min, float max, const char* format, float width = 0.0f,
                 const char* prefix = nullptr);
// An RGB colour as #RRGGBB, typed the same way. True when a new colour was typed.
bool hexField(const char* id, float rgb[3], float width);
// Text in a box, typed the same way. True when new text was typed into `buf`.
bool textField(const char* id, char* buf, size_t size, float width);

// A live search box (changes as you type, `hint` while empty). True when the text changed.
bool searchField(const char* id, const char* hint, char* buf, size_t size, float width);

// Fields that open straight into typing (a popup asking for a value), and asking about it.
void focusField(const char* id);   // the next field with this id starts typing at once
bool isEditing(const char* id);    // a field with this id is being typed into

bool radio(const char* label, bool active);   // true when clicked
bool checkbox(const char* label, bool& v);    // true when toggled

// ---- menus
// A menu entry for a registered action: its title, shortcut, check mark and enabled state.
void actionMenuItem(ActionRegistry& actions, std::string_view id);

// ---- rows
// [label] [slider] [field]. True when `v` changed.
bool sliderRow(const char* label, float& v, float min, float max, const char* format, bool logarithmic = false,
               float defaultValue = kNoDefault);

}

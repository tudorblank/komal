#pragma once
// The Color panel (Photoshop style): primary / secondary swatches, a hue ring around a
// saturation / brightness square, hex and RGB fields, and HSB sliders.
//
// Edits the clicked swatch's colour, through color.set (and color.swap). Keeps its own HSB, so a
// hue survives the colour going grey or black, and follows the colour when something else
// changes it.

class ActionRegistry;
struct ToolColors;

class ColorPanel {
public:
    void draw(ToolColors& colors, ActionRegistry& actions);
    bool& open() { return m_open; }   // shown (View > Color, the window's close button)

private:
    void syncFrom(const float rgb[3]);   // the colour changed elsewhere
    void writeTo(float rgb[3]);          // this panel's HSB, remembered as what it last wrote
    void drawSwatches(ToolColors& colors, ActionRegistry& actions, float block);
    void drawWheel(float size);
    void drawCodes();
    void drawSliders();

    float m_h = 0.0f, m_s = 0.0f, m_v = 1.0f;   // 0..1 each
    float m_last[3] = { -1.0f, -1.0f, -1.0f };  // the colour as this panel last saw / wrote it
    enum class WheelDrag : unsigned char { None, Hue, SatBright } m_wheelDrag = WheelDrag::None;
    bool  m_editSecondary = false;              // which colour the wheel edits
    bool  m_open = true;
};

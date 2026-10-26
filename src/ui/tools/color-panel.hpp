#pragma once
// The Color panel, Photoshop's colour wheel: the primary / secondary swatch pair in the corner,
// a hue ring with a saturation / brightness square inside, the colour's #hex code with its
// R G B values beside it, and Hue, Saturation and Brightness sliders whose tracks show the
// gradient they pick from.
//
// It edits one of the tools' two colours (ToolColors): the primary, or the secondary once its
// swatch is clicked. The swatches' arrow (or X) swaps them. It keeps its own hue, saturation and
// brightness, so a hue survives the colour going grey or black (RGB alone can't hold it), and
// follows the colour when something else changes it.

struct ToolColors;

class ColorPanel {
public:
    void draw(bool* open, ToolColors& colors);

private:
    void syncFrom(const float rgb[3]);   // the colour changed elsewhere
    void writeTo(float rgb[3]);
    void drawSwatches(ToolColors& colors, float block);
    void drawWheel(float size);
    void drawCodes();
    void drawSliders();

    float m_h = 0.0f, m_s = 0.0f, m_v = 1.0f;   // 0..1 each
    float m_last[3] = { -1.0f, -1.0f, -1.0f };  // the colour as this panel last saw / wrote it
    enum class WheelDrag : unsigned char { None, Hue, SatBright } m_wheelDrag = WheelDrag::None;
    bool  m_editSecondary = false;              // which colour the wheel edits
    char  m_hex[8] = "";
    bool  m_hexEditing = false;
};

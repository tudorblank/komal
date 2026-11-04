#pragma once
// A colour picked in a popup floating where it's opened (an artboard's colour): a saturation /
// brightness square, hue and alpha sliders, and the hex code. Separate from the Color panel's
// wheel, in the same style (Style::ColorPickerStyle). It edits its own copy: what it reports is
// for the caller to apply (an action), once a change is finished. A colour picked while its alpha
// is 0 turns solid.
//
//   if (picker.open(color)) ...                        // at the mouse, next draw
//   const FloatColorPick::Result r = picker.draw("##artboard-color");
//   if (r.done) actions.run(Actions::ArtboardSet, { ..., { kColor, r.color } });

#include "base/color.hpp"

class FloatColorPick {
public:
    void open(Color8 color);   // shown from the next draw, in the window it's drawn in

    struct Result {
        bool   done = false;   // a change finished (a drag let go, a code typed): apply it
        Color8 color;          // what it shows now
    };
    Result draw(const char* id);   // every frame, in the same window
    bool shown() const { return m_shown; }

private:
    float m_h = 0.0f, m_s = 0.0f, m_v = 0.0f, m_a = 0.0f;   // 0..1 each
    bool  m_pending = false;   // opened: the popup opens on the next draw
    bool  m_shown   = false;
    bool  m_squareHeld = false;
};

#pragma once
// The one way the UI draws: a surface (the window being drawn, or the screen over every window)
// and the shapes, text and pictures drawn on it, in screen pixels. Controls are drawn by their
// skins (skins.hpp) on one; panels that draw views of their own (the graph, the history tree, the
// colour ring, the canvas's overlays) draw on one too. Nothing else in the UI touches ImGui's draw
// lists. Colours and sizes come from the caller's style (ui/style): a painter has none of its own.
//
//   const Painter p = Painter::window();
//   p.fillRect(a, b, style.fill, style.rounding);
//   { const Painter::Clip clip = p.clip(a, b); p.text(at, style.text, label); }

#include <imgui.h>

#include <span>

class Painter {
public:
    static Painter window();       // the window being drawn
    static Painter foreground();   // over every window of the main viewport
    Painter faded(float opacity) const;   // the same surface, everything drawn see-through (opacity 0..1)

    // ---- shapes. `corners`: ImDrawFlags_RoundCorners* (0: all of them)
    void fillRect(ImVec2 a, ImVec2 b, ImU32 color, float rounding = 0.0f, ImDrawFlags corners = 0) const;
    void strokeRect(ImVec2 a, ImVec2 b, ImU32 color, float rounding = 0.0f, float width = 1.0f, ImDrawFlags corners = 0) const;
    // Each corner its own colour, blended across: top left, top right, bottom right, bottom left.
    void gradientRect(ImVec2 a, ImVec2 b, ImU32 topLeft, ImU32 topRight, ImU32 bottomRight, ImU32 bottomLeft) const;
    void line(ImVec2 a, ImVec2 b, ImU32 color, float width = 1.0f) const;
    void fillCircle(ImVec2 center, float radius, ImU32 color, int segments = 0) const;
    void strokeCircle(ImVec2 center, float radius, ImU32 color, float width = 1.0f, int segments = 0) const;
    void fillTriangle(ImVec2 a, ImVec2 b, ImVec2 c, ImU32 color) const;
    void fillQuad(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 color) const;
    void strokeQuad(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 color, float width = 1.0f) const;
    // A quad whose corners carry their own colours, blended across (a segment of a gradient ring).
    void gradientQuad(const ImVec2 (&corners)[4], const ImU32 (&colors)[4]) const;
    void fillPolygon(std::span<const ImVec2> points, ImU32 color) const;   // any outline, concave too
    void polyline(std::span<const ImVec2> points, ImU32 color, float width = 1.0f, bool closed = false) const;
    void arc(ImVec2 center, float radius, float from, float to, ImU32 color, float width = 1.0f, int segments = 0) const;   // radians
    void bezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, ImU32 color, float width = 1.0f, int segments = 0) const;   // cubic
    void bezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImU32 color, float width = 1.0f, int segments = 0) const;              // quadratic

    // ---- text
    void text(ImVec2 at, ImU32 color, const char* text, const char* end = nullptr) const;   // the current font and size
    void text(ImFont* font, float size, ImVec2 at, ImU32 color, const char* text, const char* end = nullptr, float wrapWidth = 0.0f) const;
    // At a size its font isn't baked at: laid out at `baked` from `at` (whole pixels), then
    // scaled to `shown` about `at` (the graph's text, at any zoom).
    void scaledText(ImFont* font, float baked, float shown, ImVec2 at, ImU32 color, const char* text, const char* end = nullptr) const;

    // ---- pictures
    void image(ImTextureID texture, ImVec2 a, ImVec2 b, ImVec2 uv0 = ImVec2(0.0f, 0.0f), ImVec2 uv1 = ImVec2(1.0f, 1.0f),
               ImU32 tint = IM_COL32_WHITE) const;   // tint: each texel's colour multiplied by it

    // Drawing only inside a..b while it lives (and inside any clip already there).
    class Clip {
    public:
        ~Clip();
        Clip(const Clip&) = delete;
        Clip& operator=(const Clip&) = delete;
    private:
        friend class Painter;
        explicit Clip(ImDrawList* list) : m_list(list) {}
        ImDrawList* m_list;
    };
    [[nodiscard]] Clip clip(ImVec2 a, ImVec2 b) const;

private:
    explicit Painter(ImDrawList* list) : m_list(list) {}
    ImU32 ink(ImU32 color) const;   // its opacity applied
    ImDrawList* m_list;
    float m_opacity = 1.0f;
};

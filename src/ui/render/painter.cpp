#include "ui/render/painter.hpp"

#include <cmath>

Painter Painter::window()     { return Painter(ImGui::GetWindowDrawList()); }
Painter Painter::foreground() { return Painter(ImGui::GetForegroundDrawList(ImGui::GetMainViewport())); }

void Painter::fillRect(ImVec2 a, ImVec2 b, ImU32 color, float rounding, ImDrawFlags corners) const
{
    m_list->AddRectFilled(a, b, color, rounding, corners);
}

void Painter::strokeRect(ImVec2 a, ImVec2 b, ImU32 color, float rounding, float width, ImDrawFlags corners) const
{
    m_list->AddRect(a, b, color, rounding, corners, width);
}

void Painter::gradientRect(ImVec2 a, ImVec2 b, ImU32 topLeft, ImU32 topRight, ImU32 bottomRight, ImU32 bottomLeft) const
{
    m_list->AddRectFilledMultiColor(a, b, topLeft, topRight, bottomRight, bottomLeft);
}

void Painter::line(ImVec2 a, ImVec2 b, ImU32 color, float width) const
{
    m_list->AddLine(a, b, color, width);
}

void Painter::fillCircle(ImVec2 center, float radius, ImU32 color, int segments) const
{
    m_list->AddCircleFilled(center, radius, color, segments);
}

void Painter::strokeCircle(ImVec2 center, float radius, ImU32 color, float width, int segments) const
{
    m_list->AddCircle(center, radius, color, segments, width);
}

void Painter::fillTriangle(ImVec2 a, ImVec2 b, ImVec2 c, ImU32 color) const
{
    m_list->AddTriangleFilled(a, b, c, color);
}

void Painter::fillQuad(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 color) const
{
    m_list->AddQuadFilled(a, b, c, d, color);
}

void Painter::strokeQuad(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 color, float width) const
{
    m_list->AddQuad(a, b, c, d, color, width);
}

void Painter::gradientQuad(const ImVec2 (&p)[4], const ImU32 (&c)[4]) const
{
    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    m_list->PrimReserve(6, 4);
    const ImDrawIdx base = ImDrawIdx(m_list->_VtxCurrentIdx);
    for (int i = 0; i < 4; ++i) m_list->PrimWriteVtx(p[i], white, c[i]);
    m_list->PrimWriteIdx(base);
    m_list->PrimWriteIdx(ImDrawIdx(base + 1));
    m_list->PrimWriteIdx(ImDrawIdx(base + 2));
    m_list->PrimWriteIdx(base);
    m_list->PrimWriteIdx(ImDrawIdx(base + 2));
    m_list->PrimWriteIdx(ImDrawIdx(base + 3));
}

void Painter::fillPolygon(std::span<const ImVec2> points, ImU32 color) const
{
    m_list->AddConcavePolyFilled(points.data(), int(points.size()), color);
}

void Painter::polyline(std::span<const ImVec2> points, ImU32 color, float width, bool closed) const
{
    m_list->AddPolyline(points.data(), int(points.size()), color, closed ? ImDrawFlags_Closed : ImDrawFlags_None, width);
}

void Painter::arc(ImVec2 center, float radius, float from, float to, ImU32 color, float width, int segments) const
{
    m_list->PathArcTo(center, radius, from, to, segments);
    m_list->PathStroke(color, ImDrawFlags_None, width);
}

void Painter::bezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, ImU32 color, float width, int segments) const
{
    m_list->AddBezierCubic(p0, p1, p2, p3, color, width, segments);
}

void Painter::bezier(ImVec2 p0, ImVec2 p1, ImVec2 p2, ImU32 color, float width, int segments) const
{
    m_list->AddBezierQuadratic(p0, p1, p2, color, width, segments);
}

void Painter::text(ImVec2 at, ImU32 color, const char* text, const char* end) const
{
    m_list->AddText(at, color, text, end);
}

void Painter::text(ImFont* font, float size, ImVec2 at, ImU32 color, const char* text, const char* end, float wrapWidth) const
{
    m_list->AddText(font, size, at, color, text, end, wrapWidth);
}

void Painter::scaledText(ImFont* font, float baked, float shown, ImVec2 at, ImU32 color, const char* text, const char* end) const
{
    const int first = m_list->VtxBuffer.Size;
    m_list->AddText(font, baked, at, color, text, end);
    // Laid out at the baked size from a truncated position: scaled to the exact size and position.
    const float k = shown / baked;
    const ImVec2 o(std::trunc(at.x), std::trunc(at.y));
    for (int i = first; i < m_list->VtxBuffer.Size; ++i) {
        ImVec2& v = m_list->VtxBuffer[i].pos;
        v = ImVec2(at.x + (v.x - o.x) * k, at.y + (v.y - o.y) * k);
    }
}

void Painter::image(ImTextureID texture, ImVec2 a, ImVec2 b, ImVec2 uv0, ImVec2 uv1) const
{
    m_list->AddImage(texture, a, b, uv0, uv1);
}

Painter::Clip Painter::clip(ImVec2 a, ImVec2 b) const
{
    m_list->PushClipRect(a, b, true);
    return Clip(m_list);
}

Painter::Clip::~Clip()
{
    m_list->PopClipRect();
}

#include "outline.hpp"

#include <imgui.h>

void OutlineItem::setPixelBounds(int minX, int minY, int maxX, int maxY)
{
    m_minX = minX; m_minY = minY; m_maxX = maxX; m_maxY = maxY;
}

void OutlineItem::drawOverlay(ImDrawList* dl, const OverlayContext& ctx)
{
    if (m_minX > m_maxX || m_minY > m_maxY) return;
    const float x0 = (float)m_minX, y0 = (float)m_minY, x1 = (float)(m_maxX + 1), y1 = (float)(m_maxY + 1);
    const Vec2 a = ctx.toScreen(x0, y0), b = ctx.toScreen(x1, y0), c = ctx.toScreen(x1, y1), d = ctx.toScreen(x0, y1);
    if (ctx.camera.rotation == 0.0f) dl->AddRect(ImVec2(a.x, a.y), ImVec2(c.x, c.y), m_color, 0.0f, 0, m_thickness);   // pixel-snapped
    else dl->AddQuad(ImVec2(a.x, a.y), ImVec2(b.x, b.y), ImVec2(c.x, c.y), ImVec2(d.x, d.y), m_color, m_thickness);
}

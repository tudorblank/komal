#include "outline.hpp"

#include <imgui.h>

void OutlineItem::setPixelBounds(int minX, int minY, int maxX, int maxY)
{
    m_minX = minX; m_minY = minY; m_maxX = maxX; m_maxY = maxY;
}

void OutlineItem::drawOverlay(ImDrawList* dl, const OverlayContext& ctx)
{
    if (m_minX > m_maxX || m_minY > m_maxY) return;
    const Vec2 a = ctx.toScreen((float)m_minX, (float)m_minY);
    const Vec2 b = ctx.toScreen((float)(m_maxX + 1), (float)(m_maxY + 1));
    dl->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), m_color, 0.0f, 0, m_thickness);
}

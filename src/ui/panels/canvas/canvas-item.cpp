#include "ui/panels/canvas/canvas-item.hpp"
#include "ui/panels/canvas/scene.hpp"
#include "ui/render/painter.hpp"

#include <imgui.h>

void CanvasItem::setZ(float z)
{
    if (z == m_z) return;
    m_z = z;
    if (m_scene) m_scene->m_sortDirty = true;
    if (drawsOnGpu()) update();
}

void CanvasItem::setVisible(bool visible)
{
    if (visible == m_visible) return;
    m_visible = visible;
    if (drawsOnGpu()) update();
}

void CanvasItem::update()
{
    if (m_scene) m_scene->touch();
}

void OverlayContext::strokeBounds(const Painter& draw, const BoundsI& bounds, Color8 color, float thickness) const
{
    const float x0 = float(bounds.minX), y0 = float(bounds.minY), x1 = float(bounds.maxX + 1), y1 = float(bounds.maxY + 1);
    const Vec2 a = toScreen(x0, y0), b = toScreen(x1, y0), c = toScreen(x1, y1), d = toScreen(x0, y1);
    if (camera.rotation == 0.0f) draw.strokeRect(ImVec2(a.x, a.y), ImVec2(c.x, c.y), color.packed(), 0.0f, thickness);
    else draw.strokeQuad(ImVec2(a.x, a.y), ImVec2(b.x, b.y), ImVec2(c.x, c.y), ImVec2(d.x, d.y), color.packed(), thickness);
}

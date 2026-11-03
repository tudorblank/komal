#include "outline.hpp"
#include "ui/render/painter.hpp"

#include <imgui.h>

void OutlineItem::setPixelBounds(const BoundsI& bounds)
{
    m_bounds = bounds;
}

void OutlineItem::drawOverlay(const Painter& draw, const OverlayContext& ctx)
{
    const Style::CanvasStyle& style = Style::canvas();
    const Color8 color = m_kind == Kind::Selection ? style.selection : m_kind == Kind::PreviewFrom ? style.previewFrom : style.previewTo;
    const float thickness = m_kind == Kind::PreviewTo ? style.previewToWidth : style.selectionWidth;
    if (m_bounds.empty()) return;
    const float x0 = float(m_bounds.minX), y0 = float(m_bounds.minY), x1 = float(m_bounds.maxX + 1), y1 = float(m_bounds.maxY + 1);
    const Vec2 a = ctx.toScreen(x0, y0), b = ctx.toScreen(x1, y0), c = ctx.toScreen(x1, y1), d = ctx.toScreen(x0, y1);
    if (ctx.camera.rotation == 0.0f) draw.strokeRect(ImVec2(a.x, a.y), ImVec2(c.x, c.y), color.packed(), 0.0f, thickness);   // pixel-snapped
    else draw.strokeQuad(ImVec2(a.x, a.y), ImVec2(b.x, b.y), ImVec2(c.x, c.y), ImVec2(d.x, d.y), color.packed(), thickness);
}

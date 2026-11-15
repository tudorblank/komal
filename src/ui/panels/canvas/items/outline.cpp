#include "ui/panels/canvas/items/outline.hpp"

void OutlineItem::setPixelBounds(const BoundsI& bounds)
{
    m_bounds = bounds;
}

void OutlineItem::drawOverlay(const Painter& draw, const OverlayContext& ctx)
{
    const Style::CanvasStyle& style = Style::canvas();
    const Color8 color = m_kind == Kind::Selection ? style.selection : m_kind == Kind::PreviewFrom ? style.previewFrom : style.previewTo;
    const float thickness = m_kind == Kind::PreviewTo ? style.previewToWidth : style.selectionWidth;
    if (!m_bounds.empty()) ctx.strokeBounds(draw, m_bounds, color, thickness);
}

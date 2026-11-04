#include "ui/panels/canvas/items/artboard-frame.hpp"
#include "ui/render/painter.hpp"
#include "ui/style/ui-style.hpp"

#include <imgui.h>

void ArtboardFrameItem::set(const Artboard& a)
{
    m_rect    = a.rect;
    m_name    = a.name;
    m_visible = a.visible;
}

void ArtboardFrameItem::drawOverlay(const Painter& draw, const OverlayContext& ctx)
{
    if (m_rect.empty()) return;
    const Style::CanvasStyle& style = Style::canvas();
    const uint8_t alpha = m_visible ? 255 : 110;   // hidden: faint
    const ImU32 edge = style.artboardFrame.withAlpha(alpha).packed(), label = style.artboardLabel.withAlpha(alpha).packed();
    const float x0 = float(m_rect.minX), y0 = float(m_rect.minY), x1 = float(m_rect.maxX + 1), y1 = float(m_rect.maxY + 1);
    const Vec2 a = ctx.toScreen(x0, y0), b = ctx.toScreen(x1, y0), c = ctx.toScreen(x1, y1), d = ctx.toScreen(x0, y1);
    if (ctx.camera.rotation == 0.0f) draw.strokeRect(ImVec2(a.x, a.y), ImVec2(c.x, c.y), edge, 0.0f, 1.0f);
    else draw.strokeQuad(ImVec2(a.x, a.y), ImVec2(b.x, b.y), ImVec2(c.x, c.y), ImVec2(d.x, d.y), edge, 1.0f);
    const float size = ImGui::GetFontSize() * 0.85f;
    draw.text(ImGui::GetFont(), size, ImVec2(a.x, a.y - size - 3.0f), label, m_name.c_str());   // above its top-left corner
}

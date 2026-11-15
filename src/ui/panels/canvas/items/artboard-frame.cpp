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
    const uint8_t alpha = m_visible ? 255 : style.artboardHiddenAlpha;
    ctx.strokeBounds(draw, m_rect, style.artboardFrame.withAlpha(alpha), 1.0f);
    const Vec2 a = ctx.toScreen(float(m_rect.minX), float(m_rect.minY));
    const float size = ImGui::GetFontSize() * style.artboardLabelScale;
    draw.text(ImGui::GetFont(), size, ImVec2(a.x, a.y - size - style.artboardLabelGap), style.artboardLabel.withAlpha(alpha).packed(),
              m_name.c_str());   // above its top-left corner
}

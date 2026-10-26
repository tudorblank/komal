#include "tool-overlay.hpp"
#include "ui/tools/tool-manager.hpp"

void ToolOverlayItem::drawOverlay(ImDrawList* dl, const OverlayContext& ctx)
{
    m_tools.active().drawOverlay(dl, ctx, { m_x, m_y });
}

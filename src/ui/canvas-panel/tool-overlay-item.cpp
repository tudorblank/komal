#include "tool-overlay-item.h"
#include "ui/tools/tool-manager.h"

void ToolOverlayItem::paintOverlay(ImDrawList* dl, const OverlayContext& ctx)
{
    m_tools.active().paintOverlay(dl, ctx, { m_x, m_y });
}

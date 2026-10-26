#include "item.hpp"
#include "canvas/scene.hpp"

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

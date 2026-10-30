#include "origin.hpp"

OriginItem::OriginItem(Color8 color)
    : m_horizontal(0.0f, 0.0f, 0.0f, 0.0f, color), m_vertical(0.0f, 0.0f, 0.0f, 0.0f, color) {}

void OriginItem::setArm(float points)
{
    if (points == m_arm || points <= 0.0f) return;
    m_arm = points;
    update();
}

bool OriginItem::prepare(CanvasFrame& frame)
{
    // One target pixel and the arm length, in world units at this zoom.
    const float pixel = 1.0f / (frame.camera.zoom * frame.pixelsPerPoint());
    const float arm   = m_arm / frame.camera.zoom;
    m_horizontal.setRect(-arm, -pixel * 0.5f, arm * 2.0f + pixel, pixel);
    m_vertical.setRect(-pixel * 0.5f, -arm, pixel, arm * 2.0f + pixel);
    const bool h = m_horizontal.prepare(frame);
    const bool v = m_vertical.prepare(frame);
    return h || v;
}

void OriginItem::drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame)
{
    m_horizontal.drawGpu(pass, frame);
    m_vertical.drawGpu(pass, frame);
}

void OriginItem::releaseGpu()
{
    m_horizontal.releaseGpu();
    m_vertical.releaseGpu();
}

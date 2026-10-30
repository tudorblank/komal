#include "rect.hpp"
#include "base/math.hpp"

#include <cstring>

RectItem::RectItem(float x, float y, float w, float h, Color8 color)
    : m_x(x), m_y(y), m_w(w), m_h(h), m_color(color) {}

void RectItem::setRect(float x, float y, float w, float h)
{
    if (x == m_x && y == m_y && w == m_w && h == m_h) return;
    m_x = x; m_y = y; m_w = w; m_h = h;
    m_upload = true;
    update();
}

void RectItem::setColor(Color8 color)
{
    if (color.packed() == m_color.packed()) return;
    m_color = color;
    m_upload = true;
    update();
}

bool RectItem::prepare(CanvasFrame& frame)
{
    const CanvasPipelines& p = frame.pipelines;
    if (!m_uniform) {
        if (!m_uniform.create(p.ctx.device, p.colorLayout, sizeof(RectParams), "Rect")) return false;   // drawn once there's memory
        m_upload = true;
    }
    if (!m_upload) return false;

    const Mat4 model = Mat4::translateScale(m_x + m_w * 0.5f, m_y + m_h * 0.5f, m_w, m_h);
    RectParams data{};
    std::memcpy(data.model, model.m, sizeof data.model);
    const auto color = m_color.floats();   // straight
    std::memcpy(data.color, color.data(), sizeof data.color);
    m_uniform.write(p.ctx.queue, &data, sizeof data);
    m_upload = false;
    return true;
}

void RectItem::drawGpu(WGPURenderPassEncoder pass, CanvasFrame& frame)
{
    if (!m_uniform || !frame.view.overlaps(m_x, m_y, m_x + m_w, m_y + m_h)) return;
    frame.bind(pass, frame.pipelines.colorPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, m_uniform.group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0);
}

#include "canvas-items.h"
#include "gfx/gfx-math.h"

#include <imgui.h>
#include <cstring>

namespace {
struct ColorUniform { float model[16]; float color[4]; };
}

// ---------------------------------------------------------------- RectItem

RectItem::RectItem(float x, float y, float w, float h, float r, float g, float b, float a)
    : m_x(x), m_y(y), m_w(w), m_h(h), m_color{ r, g, b, a } {}

void RectItem::setRect(float x, float y, float w, float h)
{
    if (x == m_x && y == m_y && w == m_w && h == m_h) return;
    m_x = x; m_y = y; m_w = w; m_h = h;
    m_upload = true;
    update();
}

void RectItem::setColor(float r, float g, float b, float a)
{
    const float c[4] = { r, g, b, a };
    if (std::memcmp(c, m_color, sizeof c) == 0) return;
    std::memcpy(m_color, c, sizeof c);
    m_upload = true;
    update();
}

bool RectItem::prepare(CanvasFrame& frame)
{
    const CanvasPipelines& p = frame.pipelines;
    if (!m_buffer) {   // GPU resources on first use
        WGPUBufferDescriptor bd{};
        bd.label = sv("Rect Item Uniform");
        bd.size  = sizeof(ColorUniform);
        bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        m_buffer = wgpuDeviceCreateBuffer(p.ctx.device, &bd);

        WGPUBindGroupEntry ge{};
        ge.binding = 0;
        ge.buffer  = m_buffer;
        ge.size    = sizeof(ColorUniform);
        WGPUBindGroupDescriptor gd{};
        gd.label      = sv("Rect Item Bind Group");
        gd.layout     = p.colorLayout;
        gd.entryCount = 1;
        gd.entries    = &ge;
        m_bindGroup = wgpuDeviceCreateBindGroup(p.ctx.device, &gd);
        m_upload = true;
    }
    if (!m_upload) return false;

    const Mat4 model = Mat4::translateScale(m_x + m_w * 0.5f, m_y + m_h * 0.5f, m_w, m_h);
    ColorUniform data{};
    std::memcpy(data.model, model.m, sizeof data.model);
    std::memcpy(data.color, m_color, sizeof data.color);
    wgpuQueueWriteBuffer(p.ctx.queue, m_buffer, 0, &data, sizeof data);
    m_upload = false;
    return true;
}

void RectItem::draw(WGPURenderPassEncoder pass, CanvasFrame& frame)
{
    if (!m_bindGroup || !frame.view.overlaps(m_x, m_y, m_x + m_w, m_y + m_h)) return;
    frame.bind(pass, frame.pipelines.colorPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, m_bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 4, 1, 0, 0);
}

void RectItem::releaseGpu()
{
    if (m_bindGroup) { wgpuBindGroupRelease(m_bindGroup); m_bindGroup = nullptr; }
    if (m_buffer)    { wgpuBufferRelease(m_buffer);       m_buffer    = nullptr; }
}

// ---------------------------------------------------------------- OutlineItem

void OutlineItem::setPixelBounds(int minX, int minY, int maxX, int maxY)
{
    m_minX = minX; m_minY = minY; m_maxX = maxX; m_maxY = maxY;
}

void OutlineItem::paintOverlay(ImDrawList* dl, const OverlayContext& ctx)
{
    if (m_minX > m_maxX || m_minY > m_maxY) return;
    const Vec2 a = ctx.toScreen((float)m_minX, (float)m_minY);
    const Vec2 b = ctx.toScreen((float)(m_maxX + 1), (float)(m_maxY + 1));
    dl->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), m_color, 0.0f, 0, m_thickness);
}

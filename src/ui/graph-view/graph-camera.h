#pragma once
#include <imgui.h>
#include <algorithm>

// How the graph reaches the screen:   screen = origin + (graph - position) * scale
//
//   position   the graph point at the panel's top-left corner (the anchor, like Excalidraw:
//              resizing, undocking or redocking the panel never moves what sits there)
//   zoom       the user's zoom; scale = zoom * content (DPI) scale, in pixels per graph unit
//   origin     where the panel's canvas sits on screen this frame
//
// Graph units are UI points at 100% zoom, so node positions and node sizes share one space
// and a DPI change scales the whole graph uniformly instead of growing nodes into each other.
class GraphCamera {
public:
    static constexpr float kMinZoom = 0.1f;
    static constexpr float kMaxZoom = 6.0f;

    ImVec2 position = ImVec2(0.0f, 0.0f);
    float  zoom     = 1.0f;

    void  setFrame(ImVec2 origin, float contentScale) { m_origin = origin; m_contentScale = contentScale; }
    float scale() const { return zoom * m_contentScale; }
    ImVec2 origin() const { return m_origin; }

    ImVec2 toScreen(ImVec2 g) const
    { const float s = scale(); return ImVec2(m_origin.x + (g.x - position.x) * s, m_origin.y + (g.y - position.y) * s); }
    ImVec2 toGraph(ImVec2 p) const
    { const float s = scale(); return ImVec2(position.x + (p.x - m_origin.x) / s, position.y + (p.y - m_origin.y) / s); }

    void panBy(ImVec2 screenDelta)
    {
        const float s = scale();
        position.x -= screenDelta.x / s;
        position.y -= screenDelta.y / s;
    }
    // Zooms by `factor` keeping the graph point under `screen` where it is.
    void zoomAt(ImVec2 screen, float factor)
    {
        const ImVec2 before = toGraph(screen);
        zoom = std::clamp(zoom * factor, kMinZoom, kMaxZoom);
        const ImVec2 after = toGraph(screen);
        position.x += before.x - after.x;
        position.y += before.y - after.y;
    }
    // Shows graph rectangle [min, max] centred in a viewport of `size` points, never above 100%.
    void frame(ImVec2 min, ImVec2 max, ImVec2 size, float margin)
    {
        const float w = std::max(max.x - min.x, 1.0f), h = std::max(max.y - min.y, 1.0f);
        const float fit = std::min((size.x - 2.0f * margin) / w, (size.y - 2.0f * margin) / h) / m_contentScale;
        zoom = std::clamp(fit, kMinZoom, 1.0f);
        const float s = scale();
        position.x = (min.x + max.x) * 0.5f - size.x * 0.5f / s;
        position.y = (min.y + max.y) * 0.5f - size.y * 0.5f / s;
    }

private:
    ImVec2 m_origin = ImVec2(0.0f, 0.0f);
    float  m_contentScale = 1.0f;
};

#pragma once
// A small map of the whole graph, bottom-right (left of the pinned master if they'd overlap),
// with the part on screen outlined.
//
//   click a node        centres the view on it
//   click / drag empty  centres the view on that point, following the mouse
//
// Draws the panel's own node items and links, scaled down: it can't disagree with the graph.

#include "graph/camera.hpp"
#include "graph/node-item/item.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <vector>

class GraphMinimap {
public:
    struct Edge { ImVec2 from, to; bool mask; };   // graph space
    using Items = absl::flat_hash_map<NodeId, NodeItem>;

    // Fits the map to the items (all but `skip`) and places it in the canvas.
    void layout(const Items& items, NodeId skip, ImVec2 canvasMin, ImVec2 canvasMax, const ImRect& avoid, float uiScale);
    bool visible() const { return m_visible; }
    bool contains(ImVec2 screen) const { return m_visible && m_rect.Contains(screen); }
    const ImRect& rect() const { return m_rect; }

    // A press on the map: centres the camera on the node under it, or on the point.
    void press(ImVec2 mouse, GraphCamera& camera, ImVec2 canvasSize) const;
    void drag(ImVec2 mouse, GraphCamera& camera, ImVec2 canvasSize) const;   // button held: follow

    void paint(ImDrawList* dl, const Project& project, const std::vector<Edge>& edges,
               const absl::flat_hash_set<NodeId>& selected, const GraphCamera& camera, ImVec2 canvasSize,
               ImVec2 mouse, bool hoverable) const;

private:
    ImVec2 toMap(ImVec2 g) const { return ImVec2(m_offset.x + g.x * m_k, m_offset.y + g.y * m_k); }
    ImVec2 toGraph(ImVec2 s) const { return ImVec2((s.x - m_offset.x) / m_k, (s.y - m_offset.y) / m_k); }
    NodeId nodeAt(ImVec2 screen) const;

    const Items* m_items = nullptr;   // the panel's, this frame
    NodeId m_skip = kNoNodeId;
    bool   m_visible = false;
    ImRect m_rect;                      // screen
    ImVec2 m_offset;                    // screen = offset + graph * k
    float  m_k = 1.0f;
    float  m_uiScale = 1.0f;
};

#pragma once
// The node graph of the Project, drawn and edited entirely in this panel: nodes carry their
// own thumbnail, properties and signal controls (see node-shape.h), and every user action
// becomes a Project call. Nothing is cached from the Project but view state (camera,
// selection, collapsed sections, z order): shapes are rebuilt from it every frame.
//
//   LMB on a node            select (Ctrl toggle, Shift chain), drag to move
//   LMB on empty space       rubber band (Ctrl toggles, Shift adds); click clears
//   drag from a port         link; drop on nothing opens the add menu, auto-connected;
//                            over a compositor a gap opens where the mouse is: drop to insert there
//   drag a layer row         reorder (lands on release); pulled out sideways it comes off as a link
//   drag a linked input      lift the link off (drop on nothing disconnects)
//   hover a link, click +    insert a node into it
//   RMB                      click: node menu / add menu; hold or drag: zoom
//   MMB drag, wheel          pan, zoom
//   Del / X                  delete selection, healing links around it
//   F / Home                 frame selection / everything;  Ctrl+A select all
//   Shift+A, Tab, dbl-click  add menu at the mouse
//
// The master compositor isn't placed in the graph: it is pinned to the panel's top-right
// corner at 100%, like a window over it, and works exactly like any compositor node there
// (layers, reordering, signals, masks, links). Links into it are dashed and fade out on the
// way. The navigator (graph-minimap.h) sits in the bottom-right corner.

#include "ui/graph-view/graph-camera.h"
#include "ui/graph-view/graph-minimap.h"
#include "ui/graph-view/graph-selection.h"
#include "ui/graph-view/node-menu.h"
#include "ui/graph-view/node-painter.h"
#include "ui/graph-view/node-shape.h"
#include "ui/graph-view/node-thumbnails.h"

#include <absl/container/flat_hash_map.h>
#include <vector>

class GpuContext;
struct InputFrame;
class Project;

class GraphPanel {
public:
    void init(GpuContext& gpu, Project& project, const InputFrame& input);
    void shutdown();
    void setUiScale(float scale) { m_uiScale = scale; }
    void draw(bool* open);
    // A file dropped at `screen` with `window` topmost there: true if it lands on the graph,
    // with the graph point under it.
    bool dropTarget(const ImGuiWindow* window, ImVec2 screen, ImVec2& graph) const;
    void selectOnly(NodeId id);

private:
    struct Hit {
        enum class Kind : uint8_t { None, Port, Widget, Node, Link, Insert, Minimap };
        Kind   kind  = Kind::None;
        NodeId node  = kNoNodeId;
        int    index = -1;   // port or widget index in the node's shape; link index
    };
    enum class Drag : uint8_t { None, Node, MoveNodes, Empty, RubberBand, Link, Scale, Offset, Layer, Minimap };

    struct GraphLink {
        NodeId    from, to;
        InputSlot slot;
        bool      mask;
        bool      toMaster;           // drawn dashed, fading out
        LinkCurve curve;              // screen
        ImVec2    fromGraph, toGraph; // port positions in each node's own space
    };
    struct LinkDrag {
        NodeId    node = kNoNodeId;   // the end that stays put
        bool      fromOutput = true;  // that end is an output: looking for an input
        InputSlot slot;               // ... or that input
        bool      pickedUp = false;   // lifted off an input
    };
    struct LinkTarget {
        bool      valid = false;
        NodeId    node = kNoNodeId;
        InputSlot slot;
        ImVec2    pos;                // screen
    };
    struct PendingAdd {
        enum class Mode : uint8_t { Free, FromOutput, ToInput, Insert };
        Mode      mode = Mode::Free;
        ImVec2    at;                 // graph
        NodeId    from = kNoNodeId, to = kNoNodeId;
        InputSlot slot;
    };
    struct ValueEdit {
        enum class Kind : uint8_t { Scale, Offset, Rename };
        Kind        kind = Kind::Scale;
        NodeId      node = kNoNodeId;
        NodeSignal  signal  = NodeSignal::Main;
        NodeChannel channel = NodeChannel::A;
        int         axis = 0;
        float       value = 0.0f;
        char        text[128] = "";
        bool        justOpened = false;
    };

    GraphPaint paintContext(ImDrawList* dl) const { return { dl, m_camera, m_uiScale }; }
    bool isMaster(NodeId id) const;
    const GraphCamera& cameraFor(NodeId id) const { return isMaster(id) ? m_pinned : m_camera; }   // whose space its shape is in
    void syncNodes();
    void layoutOverlays(ImVec2 canvasMin, ImVec2 canvasMax);   // the pinned master and the navigator
    Hit  hitNode(NodeId id, const NodeShape& s, ImVec2 graphPoint, float portRadius) const;
    void rebuildLinks(const GraphPaint& g);
    Hit  hitTest(ImVec2 mouse, const GraphPaint& g) const;
    const NodeShape* shape(NodeId id) const;
    void bringToFront(NodeId id);

    void handleMouse(const Hit& hit, bool hovered);
    void handleKeys(ImVec2 canvasSize);
    void pressNode(NodeId id);
    void pressWidget(const Hit& hit, bool doubleClick);
    void startLink(const Hit& hit);
    void updateDrag();
    LinkTarget linkTarget(const Hit& hit) const;
    uint32_t   stackPosition(NodeId id, float y) const;   // where in its stack something dropped at graph y goes
    void finishLink(const Hit& hit);
    void openAdd(const PendingAdd& add, NodeMenu::Fit fit);
    void applyAdd(NodeType type);
    void makeRoomForInsert(const PendingAdd& add, float& x);
    void openValueEdit(const ValueEdit& edit);
    void deleteSelection();
    void frameNodes(bool selectionOnly, ImVec2 canvasSize);
    void setCursor(const Hit& hit) const;
    int  activeWidget(NodeId id) const;   // index of the widget being dragged, or -1

    void paint(const GraphPaint& g, const Hit& hit, ImVec2 canvasMin, ImVec2 canvasMax);
    void paintGrid(const GraphPaint& g, ImVec2 canvasMin, ImVec2 canvasMax) const;
    void drawPopups();

    Project*    m_project = nullptr;
    const InputFrame* m_input = nullptr;   // keys and modifiers, straight from SDL
    int         m_subscription = 0;
    const ImGuiWindow* m_window = nullptr;   // this frame's, while the graph is shown
    ImVec2      m_canvasMin{};
    GraphCamera m_camera;
    GraphCamera m_pinned;              // the master's space: 100%, its shape in the top-right corner
    float       m_pinnedScroll = 0.0f; // screen pixels, when the master is taller than the panel
    ImRect      m_pinnedRect;          // the master on screen
    ImVec2      m_canvasSize;
    GraphMinimap m_minimap;
    std::vector<GraphMinimap::Edge> m_minimapEdges;
    float       m_uiScale = 1.0f;
    int         m_frameOnStart = 3;   // frames to frame everything for (the dock layout settles first)

    absl::flat_hash_map<NodeId, NodeViewState> m_viewStates;
    absl::flat_hash_map<NodeId, NodeShape>     m_shapes;   // this frame's
    std::vector<NodeId>    m_zOrder;                       // back to front
    std::vector<GraphLink> m_links;
    GraphSelection         m_selection;
    NodeThumbnails         m_thumbs;
    NodeMenu               m_menu;

    // interaction
    Drag   m_drag = Drag::None;
    Hit    m_press;
    ImVec2 m_pressMouse;          // screen
    ImVec2 m_pressGraph;
    bool   m_pressWasSelected = false;
    bool   m_pressCtrl = false, m_pressShift = false;
    NodeWidget m_pressWidget{};
    bool   m_canvasActive = false;
    std::vector<std::pair<NodeId, ImVec2>> m_moveStart;
    GraphSelection::Set m_bandBase;
    int    m_dragValue = 0;       // Offset: value at press; Layer: the row's current stack index
    LinkDrag m_link;
    struct { NodeId node = kNoNodeId; int at = -1; } m_pendingLayer;   // the gap open under a dragged link
    struct LayerDrag {
        NodeId   node   = kNoNodeId;   // the compositor
        uint32_t layer  = 0;           // stack index of the row being dragged
        float    grab   = 0.0f;        // mouse below the row's top, graph units
        bool     lifted = false;       // past the drag threshold: floats, with a gap where it lands
        int      at     = -1;          // that gap: a position among the other layers
    } m_layerDrag;
    int    m_hoveredLink = -1;    // last frame's, for the insert button
    bool   m_rightDown = false;       // a right press on the panel is in progress
    bool   m_rightZooming = false;    // ... held or dragged: it zooms, no menu on release
    ImVec2 m_rightPress;              // where (screen): the zoom's anchor
    double m_rightPressAt = 0.0;
    NodeId m_contextNode = kNoNodeId;
    PendingAdd m_pendingAdd;
    ValueEdit  m_edit;
    bool       m_editPending = false;    // open the value popup at window level next
};

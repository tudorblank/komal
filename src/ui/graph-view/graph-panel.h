#pragma once
// The node graph of the Project, drawn and edited entirely in this panel: nodes carry their
// own thumbnail, properties and signal controls (see node-shape.h), and every user action
// becomes a Project call. Nothing is cached from the Project but view state (camera,
// selection, collapsed sections, z order): shapes are rebuilt from it every frame.
//
//   LMB on a node            select (Ctrl toggle, Shift chain), drag to move
//   LMB on empty space       rubber band (Ctrl toggles, Shift adds); click clears
//   drag from a port         link; drop on nothing opens the add menu, auto-connected
//   drag a linked input      lift the link off (drop on nothing disconnects)
//   hover a link, click +    insert a node into it
//   RMB                      node menu / add menu (drag: pan)
//   MMB drag, wheel          pan, zoom
//   Del / X                  delete selection, healing links around it
//   F / Home                 frame selection / everything;  Ctrl+A select all
//   Shift+A, Tab, dbl-click  add menu at the mouse

#include "ui/graph-view/graph-camera.h"
#include "ui/graph-view/graph-selection.h"
#include "ui/graph-view/node-menu.h"
#include "ui/graph-view/node-painter.h"
#include "ui/graph-view/node-shape.h"
#include "ui/graph-view/node-thumbnails.h"

#include <absl/container/flat_hash_map.h>
#include <vector>

class GpuContext;
class Project;

class GraphPanel {
public:
    void init(GpuContext& gpu, Project& project);
    void shutdown();
    void setUiScale(float scale) { m_uiScale = scale; }
    void draw(bool* open);

private:
    struct Hit {
        enum class Kind : uint8_t { None, Port, Widget, Node, Link, Insert };
        Kind   kind  = Kind::None;
        NodeId node  = kNoNodeId;
        int    index = -1;   // port or widget index in the node's shape; link index
    };
    enum class Drag : uint8_t { None, Node, MoveNodes, Empty, RubberBand, Link, Scale, Offset, Layer };

    struct GraphLink {
        NodeId    from, to;
        InputSlot slot;
        bool      mask;
        LinkCurve curve;
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
        ImVec2    pos;                // graph
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
    void syncNodes();
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
    int         m_subscription = 0;
    GraphCamera m_camera;
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
    int    m_hoveredLink = -1;    // last frame's, for the insert button
    bool   m_rightDown = false, m_rightDragged = false;
    NodeId m_contextNode = kNoNodeId;
    PendingAdd m_pendingAdd;
    ValueEdit  m_edit;
    bool       m_editPending = false;    // open the value popup at window level next
};

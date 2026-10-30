#pragma once
// The graph's popups, one block: the add-node menu (and what the node picked gets wired to), a
// node's context menu, and the field a value or a name is typed into. Every entry runs an action.
// Opened from the graph's input, drawn every frame inside the graph's window.
//
//   menus.openAdd(GraphMenus::FromOutput{ node }, graphPos);   // a link dropped on nothing
//   menus.openContext(node);                                   // a right click on a node
//   menus.openValue(GraphMenus::Rename{ node });               // a double-clicked title

#include "document/input-slot.hpp"
#include "ui/graph-panel/node-menu.hpp"

#include <imgui.h>
#include <variant>

class ActionRegistry;
class Project;

class GraphMenus {
public:
    // What a node picked from the add menu is wired into.
    struct Free       {};
    struct FromOutput { NodeId from; };                     // fed by a dragged output: its input lands at the point
    struct ToInput    { NodeId to; InputSlot slot; };       // feeding a dragged input: its output lands at the point
    struct Insert     { NodeId from, to; InputSlot slot; }; // into a link: centred where it was clicked
    using Wiring = std::variant<Free, FromOutput, ToInput, Insert>;

    // What the value field types, and its value as typed so far (filled in when opened).
    struct Rename      { NodeId node; char text[128] = ""; };
    struct ScaleValue  { NodeId node; NodeSignal signal; NodeChannel channel; float percent = 0.0f; };
    struct OffsetValue { NodeId node; int axis; float pixels = 0.0f; };
    using Value = std::variant<Rename, ScaleValue, OffsetValue>;

    void init(const Project& project, ActionRegistry& actions);

    void openAdd(Wiring wiring, ImVec2 graphPos);   // shows the kinds that fit the wiring
    void openContext(NodeId node);
    bool openValue(Value value);   // false: no such node (or not one with that value)
    void draw();

private:
    void add(NodeType type);
    void drawContext();
    void drawValue();

    const Project*  m_project = nullptr;
    ActionRegistry* m_actions = nullptr;

    NodeMenu m_addMenu;
    Wiring   m_wiring;
    ImVec2   m_addAt;            // graph
    NodeId   m_contextNode = kNoNodeId;
    Value    m_value = Rename{ kNoNodeId };
    bool     m_valuePending = false;   // opened at window level on the next draw
    bool     m_valueJustOpened = false;
};

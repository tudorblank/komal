#pragma once
// Every kind of node in one table: what it's called, how it shows, what can feed it, how one is
// made. Everything that tells kinds apart (actions, the add menu, the graph's colours and ports,
// linking, the Project making nodes) reads it: a new kind is its node class and one row here.
// What a kind owns besides (its settings) is the class's own: NodeSettings.

#include "base/palette.hpp"
#include "node/node-types.hpp"

#include <span>
#include <string_view>
#include <type_traits>

// What links can feed a node.
enum class InputShape : uint8_t {
    None,    // nothing: it makes its own pixels (a raster)
    One,     // one input (UnaryNode)
    Stack,   // a stack of layers (CompositorNode)
};

// A node class's shape, from the class itself: the table can't disagree with it.
template <class T> constexpr InputShape inputShapeOf()
{
    if constexpr (std::is_base_of_v<UnaryNode, T>) return InputShape::One;
    else if constexpr (std::is_base_of_v<CompositorNode, T>) return InputShape::Stack;
    else return InputShape::None;
}

struct NodeKind {
    NodeType    type;
    const char* id;           // as actions write it: "raster"
    const char* label;        // as menus show it, and a new node's name: "Raster"
    const char* hint;         // the add menu's note: "pixels you paint"
    Color8      header;       // its title bar in the graph
    InputShape  inputs;       // what can feed it (inputShapeOf its class)
    bool        ownsPixels;   // made with pixel data of its own (and numbered: "Raster 2")
    NodePtr   (*make)(RasterData* pixels);   // a new one; `pixels` only for kinds that own them
};

std::span<const NodeKind> nodeKinds();                 // in NodeType order
const NodeKind& nodeKind(NodeType type);
const NodeKind* findNodeKind(std::string_view id);     // null: no such kind
inline InputShape inputShape(const Node& n) { return nodeKind(n.type()).inputs; }

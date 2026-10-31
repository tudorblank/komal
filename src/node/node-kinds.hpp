#pragma once
// Every kind of node in one table: what it's called, how it shows, what can feed it (its class's
// ports), how one is made. Everything that tells kinds apart (actions, the add menu, the graph's
// colours and ports, the Project making nodes) reads it: a new kind is its node class and one row.
// What a kind owns besides (its settings) is the class's own: NodeSettings.

#include "base/color.hpp"
#include "node/node-types.hpp"

#include <span>
#include <string_view>

struct NodeKind {
    NodeType    type;
    const char* id;           // as actions write it: "raster"
    const char* label;        // as menus show it, and a new node's name: "Raster"
    const char* hint;         // the add menu's note: "pixels you paint"
    Color8      header;       // its title bar in the graph
    Ports       ports;        // what links it takes: its class's kPorts
    bool        ownsPixels;   // made with pixel data of its own (and numbered: "Raster 2")
    NodePtr   (*make)(RasterData* pixels);   // a new one; `pixels` only for kinds that own them
};

std::span<const NodeKind> nodeKinds();                 // in NodeType order
const NodeKind& nodeKind(NodeType type);
const NodeKind* findNodeKind(std::string_view id);     // null: no such kind

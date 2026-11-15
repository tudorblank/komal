#pragma once
// Every kind of node in one table: what it's called, what can feed it (its class's ports), how its
// wet comes from that, how one is made. Everything that tells kinds apart (actions, the add menu,
// the graph's ports, the Workspace making nodes, the GPU preview) reads it.
//
// A new kind is its class (node-types), its NodeType and its row here; the rest is asked for:
//   its header's color      ui/style: GraphStyle's kindHeader switch, a palette role (won't compile without)
//   settings of its own     a NodeSettings alternative (node.hpp), then: its file tag and reader
//                           (workspace-file.cpp: won't compile without), its fits rule (fits.hpp:
//                           won't compile without), its actions and controls (project-actions, the graph)
//   its preview             its WetRule: Computed previews it as it was until an edit lands; a GPU
//                           pass of its own (canvas/preview) makes it live (tests/preview-tests)
// The tests take every row (RandomEdits, preview-tests, node-tests): a new kind is in them by itself.

#include "node/node-types.hpp"

#include <span>
#include <string_view>

// How a kind's wet comes from what feeds it: what the GPU preview rebuilds while an edit upstream
// of it is dragged. Each row says, so a new kind can't be previewed wrong by default: one the GPU
// can't rebuild is Computed, and its wet stays as it was until the edit lands.
enum class WetRule : uint8_t {
    OwnPixels,    // its own pixels (InputShape::None)
    Input,        // its input as it is (InputShape::One)
    MovedInput,   // its input shifted by its MoveSettings (InputShape::One)
    Stack,        // its layers composed, as stackDrawn() lists them (InputShape::Stack)
    Computed,     // anything else: worked out on the CPU only
};

struct NodeKind {
    NodeType    type;
    const char* id;           // as actions write it: "raster"
    const char* label;        // as menus show it, and a new node's name: "Raster"
    const char* hint;         // the add menu's note: "pixels you paint"
    Ports       ports;        // what links it takes: its class's kPorts
    bool        ownsPixels;   // made with pixel data of its own (and numbered: "Raster 2")
    WetRule     wet;          // how its wet comes from its inputs
    NodePtr   (*make)(RasterData* pixels);   // a new one; `pixels` only for kinds that own them
};

std::span<const NodeKind> nodeKinds();                 // in NodeType order
const NodeKind& nodeKind(NodeType type);
const NodeKind* findNodeKind(std::string_view id);     // null: no such kind

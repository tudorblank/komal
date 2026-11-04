#pragma once
// A workspace as a .komal file holds it, and the file's bytes. The workspace gives one
// (Workspace::saved) and is built from one (Workspace::open); this only turns it into bytes and
// back, checking on the way in that it holds together (every id it names is there).
//
// What's saved: every node (its id, kind, name, place, signal scales, settings and links), every
// raster's pixels, the master's artboards, the canvas's view, the raster being painted into, and
// the counters that keep ids and names from coming back. Not history: an opened file starts
// with nothing to undo.
//
// The file (little-endian):
//
//   magic    8 bytes  "KOMAL" 0x00 0x0D 0x0A   (the last two catch a text-mode transfer)
//   version  u32      kVersion: a reader refuses newer ones
//   chunks   until the end, each: tag (4 ASCII bytes), size (u64), that many bytes
//     "WKSP"  once, first: next node id, numbered nodes, last artboard id, master id, active
//             raster id, camera (pan x, pan y, zoom, rotation)
//     "NODE"  one per node, in creation order: id, kind ("raster"...), label, x, y, scales
//             (signal x channel), mask ids (signal x channel), input id, layer ids (bottom
//             first), settings (a tag, then its kind's fields)
//     "ARTB"  once: every artboard: id, name, rect, colour (straight RGBA8), visible
//     "TILE"  one per raster with pixels: its node id, then its tiles as raster/tile-codec writes
//             a set (each tile's tx, ty and its content, compressed)
//             (version 1: its tile count, then per tile tx, ty and its 64x64 pixels raw; still read)
//   A reader skips chunks it doesn't know: a newer writer can add some without a new version.
//
// Pixels are the tiles' own bytes (compressed losslessly), never an image format: what's saved is
// what's read back, to the byte (read back as untrusted data: made canonical, like any pixels
// from outside).

#include "document/artboard.hpp"
#include "document/canvas-camera.hpp"
#include "node/node.hpp"
#include "raster/tile-grid.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct WorkspaceFile {
    static constexpr uint32_t kVersion = 2;   // 2: tiles through raster/tile-codec

    struct Node {
        NodeId      id = kNoNodeId;
        NodeType    type = NodeType::Raster;
        std::string label;
        float       x = 0.0f, y = 0.0f;
        float       scale[kSignalCount][kChannelCount] = {};
        NodeId      mask[kSignalCount][kChannelCount] = {};
        NodeId      input = kNoNodeId;
        std::vector<NodeId> layers;   // bottom first
        NodeSettings settings;
    };
    struct Raster {
        NodeId   node = kNoNodeId;
        TileGrid tiles;
    };

    uint32_t     nextNode = 1;          // the next node's id
    uint32_t     numbered = 0;          // nodes named with a number so far ("Raster 3")
    ArtboardId   lastArtboard = kNoArtboard;
    NodeId       master = kNoNodeId;
    NodeId       active = kNoNodeId;    // the raster painted into (none: none)
    CanvasCamera camera;
    std::vector<Node>   nodes;          // creation order
    std::vector<Raster> rasters;
    Artboards           artboards;
};

std::vector<uint8_t> encodeWorkspace(const WorkspaceFile& file);
// Null, with why, if the bytes aren't a .komal file this reads, or it doesn't hold together.
std::optional<WorkspaceFile> decodeWorkspace(std::span<const uint8_t> bytes, std::string& error);

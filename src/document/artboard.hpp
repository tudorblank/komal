#pragma once
// An artboard: a rect of the canvas to work in and export from, with a colour behind it. Only the
// master has them. The Project keeps the list; which of the master's layers each holds, and where
// it sits in the master's stack, is a group of that stack (CompositorSettings::groups), so it
// follows the layers through every link edit. Its id is that group's key: one name for both, never
// converted. Each artboard has exactly one group and each group is an artboard
// (Project::artboardsMatchStack).
//
// Drawn as a fill: its colour over its rect, behind its layers (which still show past its edges).

#include "base/color.hpp"
#include "node/stack-groups.hpp"
#include "raster/tile.hpp"

#include <cstdint>
#include <string>
#include <vector>

using ArtboardId = GroupKey;   // assigned by the Project, never reused
constexpr ArtboardId kNoArtboard = kNoGroup;

struct Artboard {
    ArtboardId  id = kNoArtboard;
    std::string name;                   // "Artboard 1"
    BoundsI     rect;                   // world pixels, inclusive
    Color8      color{ 0, 0, 0, 0 };    // straight; a new one's is transparent
    bool        visible = true;         // its eye: hidden, it and its layers are left out
    bool operator==(const Artboard&) const = default;
};
using Artboards = std::vector<Artboard>;   // in the order they were made

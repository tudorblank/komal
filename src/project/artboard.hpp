#pragma once
// An artboard: a rect of the canvas to work in and export from, with a color behind it. Only the
// master has them. The Workspace keeps the list (ArtboardList); which of the master's layers each holds, and where
// it sits in the master's stack, is a group of that stack (CompositorSettings::groups), so it
// follows the layers through every link edit. Its id is that group's key: one name for both, never
// converted. Each artboard has exactly one group and each group is an artboard
// (ArtboardList::match, Workspace::artboardsMatchStack).
//
// Drawn as a fill: its color over its rect, behind its layers (which still show past its edges).

#include "base/color.hpp"
#include "node/node-types.hpp"   // GroupLook
#include "node/stack-groups.hpp"
#include "raster/tile.hpp"

#include <cstdint>
#include <string>
#include <vector>

using ArtboardId = GroupKey;   // assigned by the Workspace, never reused
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

constexpr BoundsI kDefaultArtboardRect{ 0, 0, 1919, 1079 };   // a new one with no view to fit: 1920 x 1080 from the origin

// The master's artboards as a list: ids handed out once (never back, nor their names), found by
// id, added, changed, taken out. What each is to the master's stack (a group) and what views are
// told stay the Workspace's (workspace-stacks.cpp): the list is all this holds.
class ArtboardList {
public:
    const Artboards& all() const { return m_list; }
    const Artboard*  find(ArtboardId id) const;
    ArtboardId last() const { return m_last; }   // the last id handed out

    // A new one, empty and transparent, named "Artboard <id>"; kNoArtboard if the rect doesn't fit.
    ArtboardId add(const BoundsI& rect);
    bool set(const Artboard& a);     // the one with its id; false: none, the same, or it doesn't fit
    bool remove(ArtboardId id);
    void restore(const Artboards& list) { m_list = list; }   // a step's: ids handed out stay handed out
    void open(const Artboards& list, ArtboardId last);       // a file's

    bool match(const std::vector<StackGroup>& groups) const;   // each one exactly one of `groups`, and back
    std::vector<GroupLook> looks() const;                      // how the master draws them

private:
    Artboards  m_list;   // in the order they were made
    ArtboardId m_last = kNoArtboard;
};

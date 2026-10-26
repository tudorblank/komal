#pragma once
// A raster's own history: every finished edit (a stroke, ...) is a commit holding the tiles as
// they were before it. The pixels stay here, in the raster; the project's history only refers
// to commits by id (RasterCommitRef), together with whatever else one user action changed.
//
// Flipping a commit (RasterData::flip) swaps its tiles with the raster's, so the same commit
// undoes and redoes.

#include "raster/tile-grid.hpp"

#include <memory>
#include <vector>

// Tiles as they were before an edit (nullptr state = the tile didn't exist). Applying a delta
// swaps every entry with the raster's current tile, so the same delta undoes and redoes.
struct TileDelta {
    struct Entry {
        TileKey key;
        std::unique_ptr<Tile> state;
    };
    std::vector<Entry> entries;

    void remember(TileKey key, const Tile* tile)
    {
        Entry& e = entries.emplace_back(Entry{ key, nullptr });
        if (tile) {
            e.state = std::make_unique<Tile>();
            e.state->copyFrom(*tile);
        }
    }
    void forgetLast() { entries.pop_back(); }
    bool empty() const { return entries.empty(); }

    size_t bytes() const
    {
        size_t n = 0;
        for (const Entry& e : entries) if (e.state) n += sizeof(Tile);
        return n;
    }
};

using RasterCommitId = uint32_t;
constexpr RasterCommitId kNoRasterCommit = 0xFFFFFFFFu;

// The commits of one raster, kept by id. Commits on undone branches of the project's history
// stay too: that history can come back to them.
class RasterHistory {
public:
    RasterCommitId add(TileDelta delta)   // an edit that changed nothing isn't kept
    {
        if (delta.empty()) return kNoRasterCommit;
        m_commits.push_back(std::move(delta));
        return RasterCommitId(m_commits.size() - 1);
    }
    TileDelta& at(RasterCommitId id) { return m_commits[id]; }
    size_t size() const { return m_commits.size(); }
    size_t bytes() const
    {
        size_t n = 0;
        for (const TileDelta& d : m_commits) n += d.bytes();
        return n;
    }

private:
    std::vector<TileDelta> m_commits;
};

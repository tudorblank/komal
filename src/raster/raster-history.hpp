#pragma once
// A raster's own history: each finished edit is a commit holding the tiles as they were before.
// The project's history refers to commits by id (RasterCommitRef). Flipping a commit swaps its
// tiles with the raster's, so the same commit undoes and redoes.

#include "raster/tile-grid.hpp"

#include <memory>
#include <vector>

// Tiles as they were before an edit (null state: the tile didn't exist).
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

// The commits of one raster, by id. Commits on undone branches stay: history can return to them.
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

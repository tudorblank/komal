#pragma once
// Every RasterTiles views share, made on first use: a node's output, a raster's own pixels. A
// node's tiles built for one view (the canvas, a thumbnail, a preview) are there for every other.
// One set of pages and one render context for all of them.
//
//   std::shared_ptr<RasterTiles> t = cache.output(node);   // kept while t is held, and a while after
//   cache.sync();                                          // once a frame, before any view builds
//
// An entry nobody holds goes once it's been unused for a while; a removed node's go at once.

#include "canvas/tiles/raster-tiles.hpp"

#include <absl/container/flat_hash_map.h>
#include <memory>

class TileCache {
public:
    enum class Kind : uint8_t { Output, Wet };   // what a node shows; its raw wet (a raster's own pixels)

    TileCache() = default;
    ~TileCache() { release(); }
    TileCache(const TileCache&) = delete;
    TileCache& operator=(const TileCache&) = delete;

    std::shared_ptr<RasterTiles> get(const NodePtr& node, Kind kind);
    std::shared_ptr<RasterTiles> output(const NodePtr& node) { return get(node, Kind::Output); }
    std::shared_ptr<RasterTiles> wet(const NodePtr& node) { return get(node, Kind::Wet); }
    // Tiles of another source (a stroke's coverage) on the same pages; the caller keeps them.
    std::unique_ptr<RasterTiles> make(std::unique_ptr<TileSource> source);

    void sync();               // recycles the pages, then every entry follows its source
    void forget(NodeId id);    // a removed node's entries
    void release();            // every entry's GPU tiles and the pages; holders keep empty tiles

    size_t tileCount() const;
    size_t pageCount() const { return m_pages.pageCount(); }

private:
    static constexpr double kKeepSeconds = 30.0;   // an entry nobody holds, unused this long, goes

    struct Key {
        const Node* node;
        Kind        kind;
        bool operator==(const Key&) const = default;
        template <class H> friend H AbslHashValue(H h, const Key& k) { return H::combine(std::move(h), k.node, k.kind); }
    };
    struct Entry {
        NodeId id;
        std::shared_ptr<RasterTiles> tiles;
        double lastUsed;   // Clock::seconds()
    };

    AtlasPages m_pages;    // before the entries, which free into it
    RenderCtx  m_render;
    absl::flat_hash_map<Key, Entry> m_entries;
};

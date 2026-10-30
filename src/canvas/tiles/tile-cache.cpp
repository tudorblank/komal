#include "canvas/tiles/tile-cache.hpp"
#include "base/clock.hpp"

std::shared_ptr<RasterTiles> TileCache::get(const NodePtr& node, Kind kind)
{
    auto [it, made] = m_entries.try_emplace(Key{ node.get(), kind });
    Entry& e = it->second;
    if (made) {
        std::unique_ptr<TileSource> source;
        if (kind == Kind::Output) source = std::make_unique<NodeOutputSource>(node);
        else                      source = std::make_unique<NodeWetSource>(node);
        e.id    = node->id();
        e.tiles = std::make_shared<RasterTiles>(std::move(source), m_pages, m_render);
        e.tiles->sync();   // reads the source: usable at once
    }
    e.lastUsed = Clock::seconds();
    return e.tiles;
}

std::unique_ptr<RasterTiles> TileCache::make(std::unique_ptr<TileSource> source)
{
    return std::make_unique<RasterTiles>(std::move(source), m_pages, m_render);
}

void TileCache::sync()
{
    m_pages.recycle();   // what views drew last frame is no longer read
    const double now = Clock::seconds();
    absl::erase_if(m_entries, [&](const auto& kv) { return kv.second.tiles.use_count() == 1 && now - kv.second.lastUsed > kKeepSeconds; });
    for (auto& [key, e] : m_entries) e.tiles->sync();
}

void TileCache::forget(NodeId id)
{
    absl::erase_if(m_entries, [&](const auto& kv) {
        if (kv.second.id != id) return false;
        kv.second.tiles->release();   // a holder keeps empty tiles until it lets go
        return true;
    });
}

void TileCache::release()
{
    for (auto& [key, e] : m_entries) e.tiles->release();
    m_entries.clear();
    m_pages.release();
}

size_t TileCache::tileCount() const
{
    size_t n = 0;
    for (const auto& [key, e] : m_entries) n += e.tiles->tileCount();
    return n;
}

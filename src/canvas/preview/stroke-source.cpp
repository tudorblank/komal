#include "canvas/preview/stroke-source.hpp"
#include "document/project.hpp"

const Tile* StrokeSource::tile(int tx, int ty, RenderCtx&)
{
    return m_project.stroke().coverage().getReadTile(tx, ty);
}

TileSet StrokeSource::footprint()
{
    TileSet t;
    m_project.stroke().coverage().forEachRead([&](int tx, int ty, const Tile&) { t.insert(TileUtils::packKey(tx, ty)); });
    return t;
}

bool StrokeSource::takeChanges(TileSet& changed)
{
    const StrokeSession& s = m_project.stroke();
    const bool all = !s.changesSince(m_seen, changed);
    m_seen = s.revision();
    return all;
}

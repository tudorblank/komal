#include "ui/panels/canvas/preview/stroke-source.hpp"
#include "document/workspace.hpp"

const Tile* StrokeSource::tile(int tx, int ty, RenderCtx&)
{
    return m_workspace.stroke().coverage().getReadTile(tx, ty);
}

TileSet StrokeSource::footprint()
{
    TileSet t;
    m_workspace.stroke().coverage().forEachRead([&](int tx, int ty, const Tile&) { t.insert(TileUtils::packKey(tx, ty)); });
    return t;
}

bool StrokeSource::takeChanges(TileSet& changed)
{
    const StrokeSession& s = m_workspace.stroke();
    const bool all = !s.changesSince(m_seen, changed);
    m_seen = s.revision();
    return all;
}

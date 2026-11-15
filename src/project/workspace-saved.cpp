// The Workspace as a .komal file holds it (WorkspaceFile), and back: saved() takes everything a
// file keeps; open() builds a workspace from it as init()'s seed (what it holds is the start).
// Its values passed the edits' own checks when it was decoded (project/fits.hpp); its links are
// connect()'s to allow.

#include "project/workspace.hpp"

WorkspaceFile Workspace::saved() const
{
    WorkspaceFile f;
    f.nextNode     = m_nodes.nextId();
    f.numbered     = m_nodes.numbered();
    f.lastArtboard = m_artboards.last();
    f.master       = m_master->id();
    f.active       = m_active;
    f.camera       = m_canvasCamera;
    f.artboards    = m_artboards.all();
    for (const NodePtr& n : nodes()) {
        f.nodes.push_back({ n->id(), n->type(), NodeRecord::of(*n) });
        if (const RasterRootNode* r = rasterAt(n->id())) {
            WorkspaceFile::Raster pixels;
            pixels.node = n->id();
            r->raster().forEachTile([&](int tx, int ty, const Tile& t) { pixels.tiles.writeOrCreateTile(tx, ty).copyFrom(t); });
            if (pixels.tiles.size() > 0) f.rasters.push_back(std::move(pixels));
        }
    }
    return f;
}

bool Workspace::open(const WorkspaceFile& f, std::string& error)
{
    bool ok = true;
    const auto fail = [&](const char* message) {
        if (ok) error = message;
        ok = false;
    };
    init([&](Workspace&) {
        // The master is the first node, as init() made it.
        if (f.nodes.empty() || f.nodes.front().id != m_master->id() || f.master != m_master->id())
            return fail("damaged: the master isn't first");
        for (size_t i = 1; i < f.nodes.size(); ++i) {
            const WorkspaceFile::SavedNode& n = f.nodes[i];
            if (m_nodes.create(key(), n.type, n.record.x, n.record.y, n.id) == kNoNodeId) return fail("damaged: node ids");
        }
        // Pixels: the start's own (no commit of a step).
        for (const WorkspaceFile::Raster& r : f.rasters)
            if (RasterRootNode* raster = rasterAt(r.node))
                raster->edit(key(), [&](RasterEditor& ed) { ed.applyTileGrid(r.tiles, { 255, CompositeMode::Replace }); });

        // Links, each one checked as if made by hand (no cycle, a port that takes it).
        for (const auto& [id, type, n] : f.nodes) {
            if (n.input != kNoNodeId && !connect(n.input, id, InputSlot::input())) return fail("damaged: a link that can't be");
            for (size_t i = 0; i < n.layers.size(); ++i)
                if (!connect(n.layers[i], id, InputSlot::newLayer(uint32_t(i)))) return fail("damaged: a link that can't be");
            for (int sig = 0; sig < kSignalCount; ++sig)
                for (int ch = 0; ch < kChannelCount; ++ch)
                    if (n.mask[sig][ch] != kNoNodeId && !connect(n.mask[sig][ch], id, InputSlot::mask(NodeSignal(sig), NodeChannel(ch))))
                        return fail("damaged: a link that can't be");
        }

        // Each node's own fields (NodeRecord::applyTo): settings its kind's; groups are the
        // master's alone, and they're its artboards.
        for (const auto& [id, type, n] : f.nodes) {
            Node& node = *find(id);
            if (node.settings().index() != n.settings.index()) return fail("damaged: settings of another kind");
            const auto* stack = std::get_if<CompositorSettings>(&n.settings);
            if (stack && !stack->groups.empty() && !isMaster(id)) return fail("damaged: artboards outside the master");
            n.applyTo(key(), node);
            if (node.settings() != n.settings) return fail("damaged: settings the node can't hold as they are");   // repaired: not as saved
        }
        m_artboards.open(f.artboards, f.lastArtboard);
        if (!artboardsMatchStack()) return fail("damaged: artboards and the master's stack disagree");
        showArtboards();

        m_nodes.resume(f.nextNode, f.numbered);
        m_canvasCamera = f.camera;
        m_active = kNoNodeId;
        if (f.active != kNoNodeId) setActiveRaster(f.active);
    });
    return ok;
}

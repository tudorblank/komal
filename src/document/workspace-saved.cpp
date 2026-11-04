// The Workspace as a .komal file holds it (WorkspaceFile), and back: saved() takes everything a
// file keeps; open() builds a workspace from it, through the same checked edits everything else
// uses (a link is connect()'s to allow), as init()'s seed: what it holds is the start.

#include "document/workspace.hpp"

WorkspaceFile Workspace::saved() const
{
    WorkspaceFile f;
    f.nextNode     = m_nodes.nextId();
    f.numbered     = m_nodes.numbered();
    f.lastArtboard = m_lastArtboard;
    f.master       = m_master->id();
    f.active       = m_active;
    f.camera       = m_canvasCamera;
    f.artboards    = m_artboards;
    for (const NodePtr& n : nodes()) {
        WorkspaceFile::Node s;
        s.id    = n->id();
        s.type  = n->type();
        s.label = n->label();
        s.x = n->x();
        s.y = n->y();
        const NodeLinks& in = n->links();
        for (int sig = 0; sig < kSignalCount; ++sig)
            for (int ch = 0; ch < kChannelCount; ++ch) {
                s.scale[sig][ch] = n->signal(NodeSignal(sig)).scale[ch];
                s.mask[sig][ch]  = in.masks[sig][ch] ? in.masks[sig][ch]->id() : kNoNodeId;
            }
        if (in.input) s.input = in.input->id();
        for (const NodePtr& l : in.layers) s.layers.push_back(l->id());
        s.settings = n->settings();
        f.nodes.push_back(std::move(s));
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
    const auto fail = [&](const char* why) {
        if (ok) error = why;
        ok = false;
    };
    init([&](Workspace&) {
        // The master is the first node, as init() made it.
        if (f.nodes.empty() || f.nodes.front().id != m_master->id() || f.master != m_master->id())
            return fail("damaged: the master isn't first");
        for (size_t i = 1; i < f.nodes.size(); ++i) {
            const WorkspaceFile::Node& n = f.nodes[i];
            if (m_nodes.create(key(), n.type, n.x, n.y, n.id) == kNoNodeId) return fail("damaged: node ids");
        }
        for (const WorkspaceFile::Node& n : f.nodes) {
            Node& node = *find(n.id);
            node.setPosition(key(), n.x, n.y);
            node.setLabel(key(), n.label);
            for (int sig = 0; sig < kSignalCount; ++sig)
                for (int ch = 0; ch < kChannelCount; ++ch)
                    if (node.hasSignal(NodeSignal(sig))) node.setScale(key(), NodeSignal(sig), NodeChannel(ch), n.scale[sig][ch]);
        }

        // Pixels: the start's own (no commit of a step).
        for (const WorkspaceFile::Raster& r : f.rasters)
            if (RasterRootNode* raster = rasterAt(r.node))
                raster->edit(key(), [&](RasterEditor& ed) { ed.applyTileGrid(r.tiles, { 255, CompositeMode::Replace }); });

        // Links, each one checked as if made by hand (no cycle, a port that takes it).
        for (const WorkspaceFile::Node& n : f.nodes) {
            if (n.input != kNoNodeId && !connect(n.input, n.id, InputSlot::input())) return fail("damaged: a link that can't be");
            for (size_t i = 0; i < n.layers.size(); ++i)
                if (!connect(n.layers[i], n.id, InputSlot::newLayer(uint32_t(i)))) return fail("damaged: a link that can't be");
            for (int sig = 0; sig < kSignalCount; ++sig)
                for (int ch = 0; ch < kChannelCount; ++ch)
                    if (n.mask[sig][ch] != kNoNodeId && !connect(n.mask[sig][ch], n.id, InputSlot::mask(NodeSignal(sig), NodeChannel(ch))))
                        return fail("damaged: a link that can't be");
        }

        // Settings, each its kind's. Groups are the master's alone, and they're its artboards.
        for (const WorkspaceFile::Node& n : f.nodes) {
            Node& node = *find(n.id);
            if (node.settings().index() != n.settings.index()) return fail("damaged: settings of another kind");
            const auto* stack = std::get_if<CompositorSettings>(&n.settings);
            if (stack && !stack->groups.empty() && !isMaster(n.id)) return fail("damaged: artboards outside the master");
            node.setSettings(key(), n.settings);
        }
        m_artboards = f.artboards;
        m_lastArtboard = f.lastArtboard;
        if (!artboardsMatchStack()) return fail("damaged: artboards and the master's stack disagree");
        showArtboards();

        m_nodes.resume(f.nextNode, f.numbered);
        m_canvasCamera = f.camera;
        m_active = kNoNodeId;
        if (f.active != kNoNodeId) setActiveRaster(f.active);
    });
    return ok;
}

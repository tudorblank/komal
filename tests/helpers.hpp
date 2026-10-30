#pragma once
// What several test files share: a project with its starter graph, and reading its pixels.

#include "base/log.hpp"
#include "document/project.hpp"

#include <doctest/doctest.h>
#include <string>
#include <vector>

// Catches what's logged while it lives (instead of stderr): a test can check a refusal said why.
struct LogCapture {
    LogCapture() { lines().clear(); Log::setSink([](const char* line) { lines().push_back(line); }); }
    ~LogCapture() { Log::setSink(nullptr); }
    static std::vector<std::string>& lines() { static std::vector<std::string> l; return l; }
    bool saw(const char* text) const
    {
        for (const std::string& l : lines()) if (l.find(text) != std::string::npos) return true;
        return false;
    }
};

// The starter project: a master over two rasters ("Raster 1" the background, "Raster 2" the
// painted disc, active).
struct StarterProject {
    Project project;
    NodeId  master, background, paint;

    StarterProject()
    {
        project.init();
        master = project.master().id();
        const CompositorNode& m = project.master();
        background = m.layers().at(0)->id();
        paint      = m.layers().at(1)->id();
    }
};

inline RGBA pixelOf(const Project& p, NodeId raster, int x, int y)
{
    const auto* r = dynamic_cast<const RasterRootNode*>(p.find(raster));
    REQUIRE(r);
    const Tile* t = r->raster().getReadTile(TileUtils::worldToTile(x), TileUtils::worldToTile(y));
    return t ? t->getReadPx(TileUtils::worldToLocal(x), TileUtils::worldToLocal(y)) : transparent();
}

// A whole action-like edit: one undo step.
template <class F> void step(Project& p, const char* label, F&& f)
{
    p.beginStep(label);
    f();
    p.endStep();
}

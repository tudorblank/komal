#include "ui/image-importer.hpp"
#include "project/project-actions.hpp"
#include "command/actions.hpp"
#include "base/frames.hpp"
#include "base/log.hpp"
#include "base/file.hpp"
#include "raster/raster-data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <type_traits>

void ImageImporter::open(std::string path)
{
    open(std::move(path), Placement{});
}

void ImageImporter::open(std::string path, Placement placement)
{
    {
        std::lock_guard lock(m_mutex);
        m_queued.push_back({ std::move(path), placement });
    }
    wakeMainLoop();   // it may be asleep
}

bool ImageImporter::importOnWorker(Job& j)
{
    j.name = File::stem(j.request.path);
    if (!loadImageFile(j.request.path, j.image, j.error)) return false;
    int x = 0, y = 0;   // the image's top-left in the world
    if (const auto* at = std::get_if<OnCanvas>(&j.request.placement)) {
        // Within half the world's limit (TileUtils::clampToWorld allows all of it): the image's far
        // corner stays in the world too.
        const auto cornerInWorld = [](float v) { return int(std::clamp(v, float(-TileUtils::kWorldLimit / 2), float(TileUtils::kWorldLimit / 2))); };
        x = cornerInWorld(std::round(at->x - float(j.image.width()) * 0.5f));
        y = cornerInWorld(std::round(at->y - float(j.image.height()) * 0.5f));
    }
    tilesFromImage(j.tiles, j.image.flat(), x, y);
    j.image.release();   // the tiles hold it now
    return true;
}

ImageImporter::Polled ImageImporter::poll(ActionRegistry& actions)
{
    {
        std::lock_guard lock(m_mutex);
        for (Request& r : m_queued) {
            auto job = std::make_unique<Job>();
            job->request = std::move(r);
            Job* j = job.get();
            m_workers.post([j] {
                try {
                    j->ok = importOnWorker(*j);
                } catch (const std::exception& e) {   // out of memory, most likely: this import fails, the app goes on
                    j->image.release();
                    j->tiles.clear();
                    j->error = e.what();
                    j->ok = false;
                }
                j->finished.store(true, std::memory_order_release);
                wakeMainLoop();   // poll() picks it up
            });
            m_jobs.push_back(std::move(job));
        }
        m_queued.clear();
    }

    // In the order they were asked for: the last one opened ends up on top.
    Polled out;
    while (!m_jobs.empty() && m_jobs.front()->finished.load(std::memory_order_acquire)) {
        Job& j = *m_jobs.front();
        if (j.ok) {
            // As the action writes it: target, then the point (or the node's position) it names, if any.
            const Placement& p = j.request.placement;
            ActionArgs args{ { ProjectArgs::kPath, j.request.path }, { ProjectArgs::kName, j.name } };
            std::visit([&args](const auto& at) {
                using T = std::decay_t<decltype(at)>;
                if constexpr (std::is_same_v<T, OnCanvas>)     args.append({ { ProjectArgs::kTarget, "canvas" }, { ProjectArgs::kX, at.x }, { ProjectArgs::kY, at.y } });
                else if constexpr (std::is_same_v<T, InGraph>) args.append({ { ProjectArgs::kTarget, "graph" }, { ProjectArgs::kX, at.x }, { ProjectArgs::kY, at.y } });
                else                                           args.append({ { ProjectArgs::kTarget, "default" } });
            }, p);
            args.payload = static_cast<const TileGrid*>(&j.tiles);
            if (const ActionResult r = actions.run(Actions::ImageImport, std::move(args))) out.imported.push_back({ r.madeAs<NodeId>(), p });
            else out.failed.push_back(j.name + ": the project refused it");
        } else {
            Log::error("import", "%s: %s", j.request.path.c_str(), j.error.c_str());
            out.failed.push_back(File::name(j.request.path) + ": " + j.error);
        }
        m_jobs.erase(m_jobs.begin());
    }
    return out;
}

bool ImageImporter::ready() const
{
    return !m_jobs.empty() && m_jobs.front()->finished.load(std::memory_order_acquire);
}

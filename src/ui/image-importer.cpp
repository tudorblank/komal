#include "image-importer.hpp"
#include "command/actions.hpp"
#include "base/frames.hpp"
#include "base/log.hpp"
#include "raster/raster-data.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <tuple>
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

void ImageImporter::showOpenDialog(SDL_Window* window)
{
    static const SDL_DialogFileFilter kFilters[] = {
        { "Images", "png;jpg;jpeg;bmp;tga;psd;gif" },
        { "All files", "*" },
    };
    SDL_ShowOpenFileDialog([](void* self, const char* const* files, int) {
        if (!files) return;   // error or cancelled
        for (; *files; ++files) static_cast<ImageImporter*>(self)->open(*files);
    }, this, window, kFilters, 2, nullptr, true);
}

bool ImageImporter::importOnWorker(Job& j)
{
    if (!loadImageFile(j.request.path, j.image, j.error)) return false;
    int x = 0, y = 0;   // the image's top-left in the world
    if (const auto* at = std::get_if<OnCanvas>(&j.request.placement)) {
        const auto clampWorld = [](float v) { return int(std::clamp(v, float(-TileUtils::kWorldLimit / 2), float(TileUtils::kWorldLimit / 2))); };
        x = clampWorld(std::round(at->x - j.image.width * 0.5f));
        y = clampWorld(std::round(at->y - j.image.height * 0.5f));
    }
    tilesFromImage(j.tiles, { j.image.pixels, j.image.width, j.image.height }, x, y);
    j.image.pixels = {};   // the tiles hold it now
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
                j->ok = importOnWorker(*j);
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
            // As the action writes it: target, then the point (or the node's position) it names.
            const Placement& p = j.request.placement;
            const auto [target, x, y] = std::visit([](const auto& at) {
                using T = std::decay_t<decltype(at)>;
                if constexpr (std::is_same_v<T, OnCanvas>) return std::tuple{ "canvas", at.x, at.y };
                else if constexpr (std::is_same_v<T, InGraph>) return std::tuple{ "graph", at.x, at.y };
                else return std::tuple{ "default", 0.0f, 0.0f };
            }, p);
            ActionArgs args{ { "path", j.request.path }, { "name", j.image.name }, { "target", target }, { "x", x }, { "y", y } };
            args.payload = static_cast<const TileGrid*>(&j.tiles);
            if (const ActionResult r = actions.run(Actions::ImageImport, std::move(args))) out.imported.push_back({ NodeId(r.made), p });
            else out.failed.push_back(j.image.name + ": the document refused it");
        } else {
            Log::error("import", "%s: %s", j.request.path.c_str(), j.error.c_str());
            out.failed.push_back(std::filesystem::path(j.request.path).filename().string() + ": " + j.error);
        }
        m_jobs.erase(m_jobs.begin());
    }
    return out;
}

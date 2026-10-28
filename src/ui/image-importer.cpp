#include "image-importer.hpp"
#include "core/application.hpp"
#include "raster/raster-data.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

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
    Application::wakeMainLoop();   // it may be asleep
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
    if (j.request.placement.target == Placement::Target::Canvas) {
        const auto clampWorld = [](float v) { return int(std::clamp(v, float(-TileUtils::kWorldLimit / 2), float(TileUtils::kWorldLimit / 2))); };
        x = clampWorld(std::round(j.request.placement.x - j.image.width * 0.5f));
        y = clampWorld(std::round(j.request.placement.y - j.image.height * 0.5f));
    }
    tilesFromImage(j.tiles, { j.image.pixels, j.image.width, j.image.height }, x, y);
    j.image.pixels = {};   // the tiles hold it now
    return true;
}

std::vector<ImageImporter::Imported> ImageImporter::poll(Project& project)
{
    {
        std::lock_guard lock(m_mutex);
        for (Request& r : m_queued) {
            auto job = std::make_unique<Job>();
            job->request = std::move(r);
            Job* j = job.get();
            j->done = std::async(std::launch::async, [j] {
                const bool ok = importOnWorker(*j);
                j->finished.store(true, std::memory_order_release);
                Application::wakeMainLoop();   // poll() picks it up
                return ok;
            });
            m_jobs.push_back(std::move(job));
        }
        m_queued.clear();
    }

    // In the order they were asked for: the last one opened ends up on top.
    std::vector<Imported> imported;
    while (!m_jobs.empty() && m_jobs.front()->finished.load(std::memory_order_acquire)) {
        Job& j = *m_jobs.front();
        if (j.done.get()) {   // returns at once: the worker is past its last line
            const Placement& p = j.request.placement;
            Project::ImportOptions options;
            if (p.target == Placement::Target::Graph) {   // a node where it was dropped, linked to nothing
                options.addToMaster = false;
                options.makeActive  = false;
                options.placeNode   = true;
                options.nodeX = p.x;
                options.nodeY = p.y;
            }
            imported.push_back({ project.importTiles(j.tiles, j.image.name, options), p });
            m_lastError.clear();
        } else {
            m_lastError = j.request.path + ": " + j.error;
            std::fprintf(stderr, "import failed: %s\n", m_lastError.c_str());
        }
        m_jobs.erase(m_jobs.begin());
    }
    return imported;
}

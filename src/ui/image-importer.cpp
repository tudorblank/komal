#include "image-importer.h"
#include "core/project.h"
#include "raster/raster-data.h"

#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>

void ImageImporter::open(std::string path)
{
    std::lock_guard lock(m_mutex);
    m_queued.push_back(std::move(path));
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

void ImageImporter::poll(Project& project)
{
    {
        std::lock_guard lock(m_mutex);
        for (std::string& path : m_queued) {
            auto job = std::make_unique<Job>();
            job->path = std::move(path);
            Job* j = job.get();
            j->done = std::async(std::launch::async, [j] {
                if (!loadImageFile(j->path, j->image, j->error)) return false;
                tilesFromImage(j->tiles, { j->image.pixels, j->image.width, j->image.height }, 0, 0);
                j->image.pixels = {};   // the tiles hold it now
                return true;
            });
            m_jobs.push_back(std::move(job));
        }
        m_queued.clear();
    }
    // In the order they were asked for: the last one opened ends up on top.
    while (!m_jobs.empty() && m_jobs.front()->done.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        Job& j = *m_jobs.front();
        if (j.done.get()) {
            project.importTiles(j.tiles, j.image.name);
            m_lastError.clear();
        } else {
            m_lastError = j.path + ": " + j.error;
            std::fprintf(stderr, "import failed: %s\n", m_lastError.c_str());
        }
        m_jobs.erase(m_jobs.begin());
    }
}

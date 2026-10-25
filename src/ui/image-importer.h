#pragma once
// Brings image files into the project as new rasters. Files are decoded on worker threads
// (a big PNG takes a while), then added on the main thread by poll().
//
// open() may be called from any thread: the native file dialog answers on its own thread.

#include "core/image-file.h"
#include "raster/tile-grid.h"

#include <future>
#include <mutex>
#include <string>
#include <vector>

class Project;
struct SDL_Window;

class ImageImporter {
public:
    void open(std::string path);
    void showOpenDialog(SDL_Window* window);   // File > Import Image...
    void poll(Project& project);               // once per frame, main thread
    bool busy() const { return !m_jobs.empty(); }
    const std::string& lastError() const { return m_lastError; }

private:
    struct Job {
        std::string path;
        std::future<bool> done;
        ImageFile image;       // decoded...
        TileGrid tiles;        // ...and cut into tiles, on the worker
        std::string error;
    };
    std::mutex m_mutex;
    std::vector<std::string> m_queued;            // guarded by m_mutex
    std::vector<std::unique_ptr<Job>> m_jobs;     // main thread only
    std::string m_lastError;
};

#pragma once
// Image files -> new raster nodes. Decoding and tiling run on a worker thread; poll() makes the
// node on the main thread. Placement:
//   Canvas  dropped on the canvas: centred on that world point, on top of the master, active
//   Graph   dropped on the graph: the node where it was dropped, at the world origin, unlinked
//   Default File > Import, the command line: at the world origin, on top of the master, active
//
// open() is thread-safe (the native file dialog answers on its own thread).

#include "core/image-load.hpp"
#include "core/project.hpp"
#include "raster/tile-grid.hpp"

#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct SDL_Window;

class ImageImporter {
public:
    struct Placement {
        enum class Target : uint8_t { Default, Canvas, Graph };
        Target target = Target::Default;
        float  x = 0.0f, y = 0.0f;   // Canvas: world point; Graph: the node's position
    };
    struct Imported { NodeId node; Placement placement; };

    void open(std::string path);   // Default placement
    void open(std::string path, Placement placement);
    void showOpenDialog(SDL_Window* window);    // File > Import Image...
    std::vector<Imported> poll(Project& project);   // once per frame, main thread
    bool busy() const { return !m_jobs.empty(); }
    const std::string& lastError() const { return m_lastError; }

private:
    struct Request { std::string path; Placement placement; };
    struct Job {
        Request request;
        std::future<bool> done;
        ImageFile image;       // decoded...
        TileGrid tiles;        // ...and cut into tiles where it goes, on the worker
        std::string error;
    };
    std::mutex m_mutex;
    std::vector<Request> m_queued;                // guarded by m_mutex
    std::vector<std::unique_ptr<Job>> m_jobs;     // main thread only
    std::string m_lastError;
};

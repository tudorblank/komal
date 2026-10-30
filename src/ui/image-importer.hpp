#pragma once
// Image files -> new raster nodes. Decoding and tiling run on worker threads (a couple at a
// time); poll() makes the node on the main thread, through the image.import action. Placement:
//   ToMaster   File > Import, the command line: at the world origin, on top of the master, active
//   OnCanvas   dropped on the canvas: centred on that world point, on top of the master, active
//   InGraph    dropped on the graph: the node where it was dropped, at the world origin, unlinked
//
// open() is thread-safe (the native file dialog answers on its own thread). Requests and finished
// work wake the main loop, so nothing waits on frames meanwhile.

#include "base/worker-pool.hpp"
#include "document/image-load.hpp"
#include "node/node.hpp"
#include "raster/tile-grid.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

class ActionRegistry;
struct SDL_Window;

class ImageImporter {
public:
    struct ToMaster {};
    struct OnCanvas { float x, y; };   // a world point
    struct InGraph  { float x, y; };   // the node's position
    using Placement = std::variant<ToMaster, OnCanvas, InGraph>;
    struct Imported { NodeId node; Placement placement; };
    struct Polled {
        std::vector<Imported>    imported;   // in the order they were asked for
        std::vector<std::string> failed;     // why each that failed did ("photo.png: unknown format")
    };

    void open(std::string path);   // ToMaster
    void open(std::string path, Placement placement);
    void showOpenDialog(SDL_Window* window);    // File > Import Image...
    Polled poll(ActionRegistry& actions);   // once per frame, main thread

private:
    struct Request { std::string path; Placement placement; };
    static constexpr unsigned kWorkers = 2;   // each holds a decoded image until it's tiled

    struct Job {
        Request request;
        bool ok = false;                       // written on the worker before `finished`
        std::atomic<bool> finished{ false };   // set last on the worker, then the loop is woken
        ImageFile image;       // decoded...
        TileGrid tiles;        // ...and cut into tiles where it goes, on the worker
        std::string error;
    };
    static bool importOnWorker(Job& j);   // decodes and tiles it

    std::mutex m_mutex;
    std::vector<Request> m_queued;                // guarded by m_mutex
    std::vector<std::unique_ptr<Job>> m_jobs;     // main thread only
    WorkerPool m_workers{ kWorkers };             // last: stopped before the jobs it works on go
};

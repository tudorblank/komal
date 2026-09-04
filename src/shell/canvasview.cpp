#include "canvasview.hpp"

#include <QDebug>

#ifndef _WIN32
  #include <QGuiApplication>
  #include <qpa/qplatformnativeinterface.h>
#endif

//// setup
// constructor
CanvasView::CanvasView(std::shared_ptr<Project> project, QWindow* parent)
    : QWindow(parent), m_project(std::move(project))
{
// init WGPU
#ifdef _WIN32
    setSurfaceType(QWindow::RasterSurface);
    m_gfx.init((HWND)winId());
#else
    QByteArray platform = QGuiApplication::platformName().toLower().toLatin1();
    QPlatformNativeInterface* native = QGuiApplication::platformNativeInterface();

    setSurfaceType(QWindow::RasterSurface);

    if(platform.contains("wayland"))
    {
        void* display = native->nativeResourceForIntegration("wl_display");
        void* surface = native->nativeResourceForWindow("surface", this);
        m_gfx.init("wayland", display, surface);
    }
    else // xcb
    {
        void* display = native->nativeResourceForWindow("display", this);
        void* xwindow = (void*)(uintptr_t)winId();
        m_gfx.init("xcb", display, xwindow);
    }
#endif

    m_resizeDebounce = new QTimer(this);
    m_resizeDebounce->setSingleShot(true);
    m_resizeDebounce->setInterval(80);
    connect(m_resizeDebounce, &QTimer::timeout, this, [this]() {
        reconfigureSurface();
    });

    if(!m_gfx.m_initialized) return;
    m_gfx.configSurface(width(), height());
    m_gfx.passContext(m_camera);

    // camera
    m_camera.create();
    m_camera.update((float)width(), (float)height());
    m_camera.createScreen(width(), height());

    // gfx pipelines
    m_gfx.initIndexBuffer();
    m_gfx.m_COLSYS.createRenderPipeline(m_camera.m_bindLayout);
    m_gfx.m_TEXSYS.initAtlasSampler();
    m_gfx.m_TEXSYS.createRenderPipeline(m_camera.m_bindLayout);
    m_gfx.m_LINSYS.createRenderPipeline(m_camera.m_screenBindLayout); 
    m_gfx.m_BLURSYS.createComputePipeline();

    // timer
    m_perfLogTimer.start();
    m_renderTimer = new QTimer(this);
    m_renderTimer->setInterval(8);  // ~125 Hz
        connect(m_renderTimer, &QTimer::timeout, this, [this]() {
        if(!m_needsRender) return;
        m_needsRender = false;

        if(m_camera.panDirty)
        {
            m_camera.pan.x += m_camera.pendingPan.x;
            m_camera.pan.y += m_camera.pendingPan.y;
            m_camera.pendingPan = {0.0f, 0.0f};
            m_camera.panDirty = false;
            m_camera.update((float)width(), (float)height());
        }

        QElapsedTimer stepTimer;

        stepTimer.start();
        size_t tileCount = syncCompositedOutput();
        m_perf.syncNs += stepTimer.nsecsElapsed();
        m_perf.syncTileCount += tileCount;

        stepTimer.restart();
        renderFrame();
        m_perf.renderNs += stepTimer.nsecsElapsed();
        m_perf.frameCalls++;

        logPerfIfDue();
    });
    m_renderTimer->start();
}
void CanvasView::reconfigureSurface()
{
    if(!m_gfx.m_initialized || width() <= 0 || height() <= 0) return;
    m_gfx.configSurface(width(), height());
    m_camera.update((float)width(), (float)height());
    m_camera.updateScreen(width(), height());
    markDirty();
}

//// raster data core
void CanvasView::syncTileToGPU(uint64_t key)
{
    Key::XY pos = Key::unpack(key);
    const Chunk& tile = m_project->m_masterCompositor->getCachedTile(pos.x, pos.y);
    m_gfx.m_TEXSYS.syncChunk(0, key,
        (float)(pos.x * Grid::CHUNK_SIZE), (float)(pos.y * Grid::CHUNK_SIZE), tile.data);
}
void CanvasView::syncTilesImmediate(const std::unordered_set<uint64_t>& keys)
{
    for(uint64_t key : keys)
    {
        syncTileToGPU(key);
        m_deferredSyncKeysMaster.erase(key);
    }
}
size_t CanvasView::syncCompositedOutput()
{
    for(auto [cx, cy] : m_project->m_masterCompositor->drainDirtySyncTiles())
        m_deferredSyncKeysMaster.insert(Key::pack(cx, cy));

    if(m_project->m_masterCompositor->needsGPUBake())
    {
        BoundsI bounds = m_project->m_masterCompositor->computeBounds();
        if(bounds.valid)
        {
            int minCX = Grid::worldToChunk(bounds.minX);
            int maxCX = Grid::worldToChunk(bounds.maxX);
            int minCY = Grid::worldToChunk(bounds.minY);
            int maxCY = Grid::worldToChunk(bounds.maxY);
            for(int cy = minCY; cy <= maxCY; cy++)
                for(int cx = minCX; cx <= maxCX; cx++)
                    m_deferredSyncKeysMaster.insert(Key::pack(cx, cy));
        }
        m_project->m_masterCompositor->markGPUBaked();
    }

    int viewMinCX = Grid::worldToChunk((int)std::floor(-m_camera.pan.x / m_camera.zoom));
    int viewMaxCX = Grid::worldToChunk((int)std::ceil((width()  - m_camera.pan.x) / m_camera.zoom));
    int viewMinCY = Grid::worldToChunk((int)std::floor(-m_camera.pan.y / m_camera.zoom));
    int viewMaxCY = Grid::worldToChunk((int)std::ceil((height() - m_camera.pan.y) / m_camera.zoom));

    QElapsedTimer budgetTimer;
    budgetTimer.start();
    constexpr qint64 kSyncBudgetMs = 4; // leave headroom in the ~8ms tick for render + overhead

    size_t processed = 0;
    for(auto it = m_deferredSyncKeysMaster.begin(); it != m_deferredSyncKeysMaster.end(); )
    {
        if(budgetTimer.elapsed() >= kSyncBudgetMs) break;

        Key::XY pos = Key::unpack(*it);
        bool visible = pos.x >= viewMinCX && pos.x <= viewMaxCX
                    && pos.y >= viewMinCY && pos.y <= viewMaxCY;

        if(!visible) { ++it; continue; }

        const Chunk& tile = m_project->m_masterCompositor->getCachedTile(pos.x, pos.y);
        m_gfx.m_TEXSYS.syncChunk(0, *it,
            (float)(pos.x * Grid::CHUNK_SIZE), (float)(pos.y * Grid::CHUNK_SIZE), tile.data);

        processed++;
        it = m_deferredSyncKeysMaster.erase(it);
    }

    if(!m_deferredSyncKeysMaster.empty())
        markDirty(); // keep draining the backlog next tick

    return processed;
}

void CanvasView::interpDraw(RasterRootNode& targetNode, RGBA color)
{
    int x0 = (int)std::floor(m_mouse.prevWorld.x);
    int y0 = (int)std::floor(m_mouse.prevWorld.y);
    int x1 = (int)std::floor(m_mouse.world.x);
    int y1 = (int)std::floor(m_mouse.world.y);

    int dx = std::abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    int x = x0, y = y0;
    while(true)
    {
        targetNode.paintPixel(x, y, color);
        if(x == x1 && y == y1) break;

        int e2 = 2 * err;
        if(e2 >= dy) { err += dy; x += sx; }
        if(e2 <= dx) { err += dx; y += sy; }
    }
}

// render
void CanvasView::renderFrame()
{
    if(!m_gfx.m_initialized) return;

    std::vector<BoundsI> boxes;
    boxes.reserve(m_project->m_rawRasters.size()); // boxes count = layer count

    // for each layer, store raster bounds in [boxes]
    for(RasterData& layer : m_project->m_rawRasters)
    {
        BoundsI& b = layer.getPixelBounds();
        if(b.valid) boxes.push_back(b);
    }

    m_gfx.m_LINSYS.updateBoundsLines(boxes, m_camera.pan, m_camera.zoom);
    m_gfx.m_showBBOXs = !boxes.empty();

    m_gfx.renderPass(width(), height(), m_camera);
}

// debug
void CanvasView::logPerfIfDue()
{
    if(m_perfLogTimer.elapsed() < 1000) return;

    double interpDrawAvgMs = m_perf.interpDrawCalls
        ? (m_perf.interpDrawNs / 1e6) / m_perf.interpDrawCalls : 0.0;
    double syncAvgMsPerFrame = m_perf.frameCalls
        ? (m_perf.syncNs / 1e6) / m_perf.frameCalls : 0.0;
    double syncAvgMsPerTile = m_perf.syncTileCount
        ? (m_perf.syncNs / 1e6) / (double)m_perf.syncTileCount : 0.0;
    double renderAvgMs = m_perf.frameCalls
        ? (m_perf.renderNs / 1e6) / m_perf.frameCalls : 0.0;

    qDebug("perf/1s | interpDraw: n=%d avg=%.3fms  |  sync: frames=%d avg=%.3fms/frame "
           "(%zu tiles, %.3fms/tile)  |  render: avg=%.3fms",
           m_perf.interpDrawCalls, interpDrawAvgMs,
           m_perf.frameCalls, syncAvgMsPerFrame,
           m_perf.syncTileCount, syncAvgMsPerTile,
           renderAvgMs);

    m_perf = PerfStats{};
    m_perfLogTimer.restart();
}
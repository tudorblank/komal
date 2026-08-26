#include "canvas.hpp"

#ifndef _WIN32
  #include <QGuiApplication>
  #include <qpa/qplatformnativeinterface.h>
#endif

// constructor
CanvasWindow::CanvasWindow(QWindow* parent) : QWindow(parent)
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

    if(!m_gfx.m_initialized) return;
    m_gfx.configSurface(width(), height());

    m_gfx.passContext(m_camera);

    // camera
    m_camera.create();
    m_camera.update((float)width(), (float)height());
    m_camera.createScreen(width(), height());

    // pipelines
    m_gfx.initIndexBuffer();
    m_gfx.m_COLSYS.createRenderPipeline(m_camera.m_bindLayout);
    m_gfx.m_TEXSYS.initAtlasSampler();
    m_gfx.m_TEXSYS.createRenderPipeline(m_camera.m_bindLayout);
    m_gfx.m_LINSYS.createRenderPipeline(m_camera.m_screenBindLayout); 
    
    m_layers.emplace_back();
    RasterData& background = m_layers[0];
    for(int y = 0; y < 200; y++)
        for(int x = 0; x < 200; x++)
            background.setPixel(x, y, RGBA{245, 40, 145, 255});

    m_layers.emplace_back(); // layer 1 - draw

    m_compositor = CompositorNode::create();
    m_compositor->setMain(true); // master compositor

    for(auto& raster : m_layers)
    {
        auto node = RasterRootNode::create(&raster);
        m_compositor->addLayer(node);
        m_layerNodes.push_back(node);
    }

    m_layerNodes[1]->m_opacity = 0.5f;

    syncCompositedOutput();

    renderFrame(); // first render

    // timer
    m_renderTimer = new QTimer(this);
    m_renderTimer->setInterval(8);  // ~125 Hz
        connect(m_renderTimer, &QTimer::timeout, this, [this]() {
        if(!m_needsRender) return;

        if(m_camera.panDirty)
        {
            m_camera.pan.x += m_camera.pendingPan.x;
            m_camera.pan.y += m_camera.pendingPan.y;
            m_camera.pendingPan = {0.0f, 0.0f};
            m_camera.panDirty = false;
            m_camera.update((float)width(), (float)height());
        }

        syncCompositedOutput();
        renderFrame();

        m_needsRender = false;
    });
    m_renderTimer->start();
}

// events
void CanvasWindow::resizeEvent(QResizeEvent*)
{
    if(!m_gfx.m_initialized) return;
    if(width() <= 0 || height() <= 0) return;

    m_gfx.configSurface(width(), height());

    m_camera.update((float)width(), (float)height());
    m_camera.updateScreen(width(), height());
    
    markDirty();
}
void CanvasWindow::mousePressEvent(QMouseEvent* e)
{
    if(e->button() == Qt::LeftButton)
    {
        m_mouse.leftDown = true;
        m_mouse.screen.x = e->position().x();
        m_mouse.screen.y = e->position().y();
        m_mouse.world.x = (e->position().x() - m_camera.pan.x) / m_camera.zoom;
        m_mouse.world.y = (e->position().y() - m_camera.pan.y) / m_camera.zoom;
        m_mouse.prevWorld = m_mouse.world;
    }
    else if(e->button() == Qt::RightButton)
    {
        m_mouse.rightDown = true;
        m_mouse.screen.x = e->position().x();
        m_mouse.screen.y = e->position().y();
        m_mouse.world.x = (e->position().x() - m_camera.pan.x) / m_camera.zoom;
        m_mouse.world.y = (e->position().y() - m_camera.pan.y) / m_camera.zoom;
        m_mouse.prevWorld = m_mouse.world;
    }
    else if(e->button() == Qt::MiddleButton)
    {
        m_mouse.middleDown = true;
        m_mouse.screen.x = e->position().x();
        m_mouse.screen.y = e->position().y();
        m_mouse.world.x = (e->position().x() - m_camera.pan.x) / m_camera.zoom;
        m_mouse.world.y = (e->position().y() - m_camera.pan.y) / m_camera.zoom;
        m_mouse.prevWorld = m_mouse.world;
    }
}
void CanvasWindow::mouseReleaseEvent(QMouseEvent* e)
{
    if(e->button() == Qt::LeftButton)
    {
        m_mouse.leftDown = false;
        markDirty();
    }
    else if(e->button() == Qt::RightButton)
    {
        m_mouse.rightDown = false;
        markDirty();
    }
    else if(e->button() == Qt::MiddleButton)
    {
        m_mouse.middleDown = false;
        markDirty();
    }
}
void CanvasWindow::mouseMoveEvent(QMouseEvent* e)
{
    float x = e->position().x();
    float y = e->position().y();
    float deltaX = x - m_mouse.screen.x;
    float deltaY = y - m_mouse.screen.y;

    if(m_mouse.middleDown)
    {
        m_camera.pendingPan.x += deltaX;
        m_camera.pendingPan.y += deltaY;

        m_camera.panDirty = true;
        markDirty();
    }

    m_mouse.screen.x = x;
    m_mouse.screen.y = y;
    m_mouse.world.x = (x - m_camera.pan.x) / m_camera.zoom;
    m_mouse.world.y = (y - m_camera.pan.y) / m_camera.zoom;

    if(m_mouse.leftDown)
    {
        interpDraw(m_layers[1], {255,255,255,255});
        markDirty();
    }
    else if(m_mouse.rightDown)
    {
        m_layers[1].erasePixel((int)m_mouse.world.x, (int)m_mouse.world.y);
        markDirty();
    }

    m_mouse.prevWorld = m_mouse.world; 
}
void CanvasWindow::wheelEvent(QWheelEvent* e)
{
    if(e->modifiers() & Qt::ControlModifier)
    {
        float delta = e->angleDelta().y();
        float zoomFactor = (delta > 0) ? 1.1f : 0.9f;
        float newZoom = qBound(0.05f, m_camera.zoom * zoomFactor, 50.0f);
        if(newZoom == m_camera.zoom) return;

        float actualFactor = newZoom / m_camera.zoom;

        // anchor zoom to cursor pos
        float mx = e->position().x();
        float my = e->position().y();
        m_camera.pan.x = mx + (m_camera.pan.x - mx) * actualFactor;
        m_camera.pan.y = my + (m_camera.pan.y - my) * actualFactor;

        m_camera.zoom = newZoom;

        m_camera.update((float)width(), (float)height());
        markDirty();
    }
    else
    {
        m_camera.pan.x += e->angleDelta().x() * 0.2f;
        m_camera.pan.y += e->angleDelta().y() * 0.2f;

        m_camera.update((float)width(), (float)height());
        markDirty();
    }
}

// raster data core
void CanvasWindow::syncCompositedOutput()
{
    std::vector<std::pair<int,int>> dirtyTiles;

    for(size_t i = 0; i < m_layers.size(); i++)
    {
        RasterData& raster = m_layers[i];
        for(uint64_t key : raster.m_dirtyChunkKeys)
        {
            Key::XY pos = Key::unpack(key);

            m_layerNodes[i]->invalidateTile(pos.x, pos.y);
            dirtyTiles.emplace_back(pos.x, pos.y);

            auto it = raster.m_chunkIndexMap.find(key);
            if(it != raster.m_chunkIndexMap.end())
                raster.m_chunkPool.getChunk(it->second).dirty = false;
        }
        raster.m_dirtyChunkKeys.clear();
    }

    // structural change -> rebake everything under current bounds
    if(m_compositor->needsGPUBake())
    {
        BoundsI bounds = m_compositor->computeBounds();
        if(bounds.valid)
        {
            int minCX = Grid::worldToChunk(bounds.minX);
            int maxCX = Grid::worldToChunk(bounds.maxX);
            int minCY = Grid::worldToChunk(bounds.minY);
            int maxCY = Grid::worldToChunk(bounds.maxY);

            for(int cy = minCY; cy <= maxCY; cy++)
                for(int cx = minCX; cx <= maxCX; cx++)
                    dirtyTiles.emplace_back(cx, cy);
        }
        m_compositor->markGPUBaked();
    }

    for(auto [chunkX, chunkY] : dirtyTiles)
    {
        uint64_t key = Key::pack(chunkX, chunkY);
        const Chunk& tile = m_compositor->getTileBuffer(chunkX, chunkY);

        m_gfx.m_TEXSYS.syncChunk(0, key,
            (float)(chunkX * Chunk::SIZE), (float)(chunkY * Chunk::SIZE),
            tile.data);
    }
}

void CanvasWindow::interpDraw(RasterData& inputRaster, RGBA color)
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
        inputRaster.setPixel(x, y, color);
        if(x == x1 && y == y1) break;

        int e2 = 2 * err;
        if(e2 >= dy) { err += dy; x += sx; }
        if(e2 <= dx) { err += dx; y += sy; }
    }
}

// main
void CanvasWindow::renderFrame()
{
    if(!m_gfx.m_initialized) return;

    std::vector<BoundsI> boxes;
    boxes.reserve(m_layers.size()); // boxes count = layer count

    // for each layer, store rasterdata bounds in [boxes]
    for(RasterData& layer : m_layers)
    {
        BoundsI& b = layer.getPixelBounds();
        if(b.valid) boxes.push_back(b);
    }

    m_gfx.m_LINSYS.updateBoundsLines(boxes, m_camera.pan, m_camera.zoom);
    m_gfx.m_showBBOXs = !boxes.empty();

    m_gfx.renderPass(width(), height(), m_camera);
}
#include "canvas.hpp"

// constructor
CanvasWindow::CanvasWindow(QWindow* parent) : QWindow(parent)
{
    // init gfx
    setSurfaceType(QWindow::VulkanSurface);
    m_gfx.init((HWND)winId());
    if(!m_gfx.m_initialized) return;
    m_gfx.configSurface(width(), height());

    // camera
    m_camera.create(m_gfx.m_device, m_gfx.m_queue);
    m_camera.update(m_gfx.m_queue, (float)width(), (float)height());
    m_camera.createScreen(m_gfx.m_device, m_gfx.m_queue, width(), height());

    // pipelines
    m_gfx.createGeometry();
    m_gfx.createColorBindLayout();
    m_gfx.createTexBindLayout();
    m_gfx.initAtlas();
    m_gfx.createShaders();
    m_gfx.createColorPipeline(m_camera.m_bindLayout);
    m_gfx.createTexPipeline(m_camera.m_bindLayout);
    m_gfx.createLinePipeline(m_camera.m_screenBindLayout); 
    
    // test data
        // two layers
        m_layers.reserve(2);

        // layer 0
        m_layers.emplace_back();
        RasterData& background = m_layers[0];
        for(int y = 0; y < 200; y++)
            for(int x = 0; x < 200; x++)
                background.setPixel(x, y, RGBA{245, 40, 145, 255});

        // layer 1 - draw
        m_layers.emplace_back();

        // sync before first render
        for(size_t i = 0; i < m_layers.size(); i++) syncRasterData(m_layers[i], i);
    //

    render(); // first render

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
            m_camera.update(m_gfx.m_queue, (float)width(), (float)height());
        }
        for(size_t i = 0; i < m_layers.size(); i++) syncRasterData(m_layers[i], i);
        render();

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

    m_camera.update(m_gfx.m_queue, (float)width(), (float)height());
    m_camera.updateScreen(m_gfx.m_queue, width(), height());
    
    render();
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

        m_camera.update(m_gfx.m_queue, (float)width(), (float)height());
        markDirty();
    }
    else
    {
        m_camera.pan.x += e->angleDelta().x() * 0.2f;
        m_camera.pan.y += e->angleDelta().y() * 0.2f;

        m_camera.update(m_gfx.m_queue, (float)width(), (float)height());
        markDirty();
    }
}

// raster data core
void CanvasWindow::syncRasterData(RasterData& raster, size_t layerIndex)
{
    if(layerIndex >= m_gfx.m_chunkTexObjects.size())
        m_gfx.m_chunkTexObjects.resize(layerIndex + 1);

    auto& texMap = m_gfx.m_chunkTexObjects[layerIndex];
    AtlasSet& atlas = m_gfx.atlasForLayer(layerIndex);

    for(uint64_t key : raster.m_dirtyChunkKeys)
    {
        auto it = raster.m_chunkHandleMap.find(key);
        if(it == raster.m_chunkHandleMap.end()) continue;
        Chunk& chunk = raster.m_chunkPool.getChunk(it->second);

        auto renderIt = texMap.find(key);
        if(renderIt == texMap.end())
        {
            float worldX = (float)(chunk.cPosX * Chunk::SIZE);
            float worldY = (float)(chunk.cPosY * Chunk::SIZE);
            texMap.emplace(key,
                m_gfx.buildAtlasTexObject(atlas, key, worldX, worldY,
                                           (float)Chunk::SIZE, (float)Chunk::SIZE, chunk.data));
        }
        else m_gfx.updateTexObject(renderIt->second, atlas, chunk.data);

        chunk.dirty = false;
    }
    raster.m_dirtyChunkKeys.clear();
}

void CanvasWindow::interpDraw(RasterData& inputRaster, RGBA color)
{
    float x0 = m_mouse.prevWorld.x;
    float y0 = m_mouse.prevWorld.y;
    float x1 = m_mouse.world.x;
    float y1 = m_mouse.world.y;
    
    float dx = x1 - x0, dy = y1 - y0;
    float dist = std::sqrt(dx*dx + dy*dy);
    int steps = std::max(1, (int)std::ceil(dist));

    for(int i = 0; i <= steps; i++)
    {
        float t = (float)i / steps;
        int px = (int)(x0 + dx * t);
        int py = (int)(y0 + dy * t);
        inputRaster.setPixel(px, py, color);
    }
}

// MAIN
void CanvasWindow::render()
{
    if(!m_gfx.m_initialized) return;

    std::vector<BoundsI> boxes;
    boxes.reserve(m_layers.size());
    for(RasterData& layer : m_layers)
    {
        BoundsI& b = layer.returnPixelBounds();
        if(b.valid) boxes.push_back(b);
    }

    m_gfx.updateBoundsLines(boxes, m_camera.pan, m_camera.zoom);
    m_gfx.m_showBoundsLine = !boxes.empty();

    m_gfx.renderPass(width(), height(), m_camera);
}
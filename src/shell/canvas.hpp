#pragma once

#include <QWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QMatrix4x4>

#include <QMouseEvent>
#include <QWheelEvent>

#include <webgpu.h>
#include "gfx/gfxdevice.hpp"

#include "raster/raster-utils.hpp"
#include "raster/raster-base.hpp"
#include "raster/raster-imgio.hpp"

#include "node/node-base.hpp"
#include "node/node-compositor.hpp"

#include "input.hpp"
#include "graphview.hpp"

#include <vector>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <unordered_set>

struct PerfStats{
    qint64 interpDrawNs = 0;
    int    interpDrawCalls = 0;

    qint64 syncNs = 0;
    size_t syncTileCount = 0;

    qint64 renderNs = 0;
    int    frameCalls = 0;
};

class CanvasWindow : public QWindow{
    Q_OBJECT
public:
    // constructor
    explicit CanvasWindow(QWindow* parent = nullptr);
    // destructor
    ~CanvasWindow() { if(m_renderTimer) m_renderTimer->stop(); }

    GraphSnapshot buildGraphSnapshot() const;

private:
    MouseHandler m_mouse;

    GFXDevice m_gfx;
    Camera m_camera;

    // nodes - rasters
    std::deque<RasterData> m_rawRasters;
    std::vector<std::shared_ptr<RasterRootNode>> m_rasterNodes;
    std::shared_ptr<MoveNode> m_moveNode;
    std::shared_ptr<BlurNode> m_blurNode;
    std::shared_ptr<CompositorNode> m_masterCompositor;
    void setupNodes();

    void interpDraw(RasterRootNode& targetNode, RGBA color);
    
    // sync
    std::unordered_set<uint64_t> m_deferredSyncKeysMaster;
    size_t syncCompositedOutput(); // returns dirty-tile count, for perf logging
    void syncTileToGPU(uint64_t key);
    void syncTilesImmediate(const std::unordered_set<uint64_t>& keys);
    void renderFrame();
    
    QTimer* m_renderTimer = nullptr;
    bool m_needsRender = false;
    void markDirty() { m_needsRender = true; }

    // debug
    PerfStats m_perf;
    QElapsedTimer m_perfLogTimer;
    void logPerfIfDue();

protected:
    // events
    void resizeEvent(QResizeEvent* e) override
    {
        if(!m_gfx.m_initialized) return;
        if(width() <= 0 || height() <= 0) return;

        m_gfx.configSurface(width(), height());

        m_camera.update((float)width(), (float)height());
        m_camera.updateScreen(width(), height());
        
        markDirty();
    }
    void mousePressEvent(QMouseEvent* e) override
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
    void mouseReleaseEvent(QMouseEvent* e) override
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
    void mouseMoveEvent(QMouseEvent* e) override
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
            QElapsedTimer t; t.start();
            interpDraw(*m_rasterNodes[1], {255,255,255,255});
            m_perf.interpDrawNs += t.nsecsElapsed();
            m_perf.interpDrawCalls++;
            markDirty();
        }
        else if(m_mouse.rightDown)
        {
            m_rasterNodes[1]->erasePixel((int)m_mouse.world.x, (int)m_mouse.world.y);
            markDirty();
        }

        m_mouse.prevWorld = m_mouse.world; 
    } 
    void wheelEvent(QWheelEvent* e) override
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
    void keyPressEvent(QKeyEvent* e) override
    {
        if(e->key() == Qt::Key_A)
        {
            if(m_rasterNodes.size() >= 2)
            {
                auto affected = m_masterCompositor->moveLayer(0, 1);
                syncTilesImmediate(affected);
                markDirty();
            }
            qDebug() << "LAYERS MOVED";
        }
        if(e->key() == Qt::Key_Z)
        {
            auto affected = m_moveNode->setOffset(500, 500);
            syncTilesImmediate(affected);
            markDirty();
        }
        if(e->key() == Qt::Key_B)
        {
            auto affected = m_moveNode->setOffset(1000, 1000);
            syncTilesImmediate(affected);
            markDirty();
        }
        if(e->key() == Qt::Key_I)
        {
            loadImageIntoRaster("test.png", m_rawRasters[1], 0, 0);

            m_rasterNodes[1]->invalidateNode();

            std::unordered_set<uint64_t> affected;
            m_rasterNodes[1]->collectOccupiedTiles(affected);
            syncTilesImmediate(affected);
            markDirty();
        }
        if(e->key() == Qt::Key_E) // export test
        {
            exportRasterToImage(m_rawRasters[1], "output.png");
        }
    }
};
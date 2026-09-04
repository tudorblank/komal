#pragma once

#include <QWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QExposeEvent>

#include <webgpu.h>
#include "gfx/gfxdevice.hpp"

#include "raster/raster-utils.hpp"
#include "raster/raster-base.hpp"
#include "raster/raster-imgio.hpp"

#include "node/node-base.hpp"
#include "node/node-compositor.hpp"

#include "project.hpp"

#include <memory>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <unordered_set>

struct PerfStats{
    qint64 interpDrawNs = 0;
    int interpDrawCalls = 0;

    qint64 syncNs = 0;
    size_t syncTileCount = 0;

    qint64 renderNs = 0;
    int frameCalls = 0;
};

struct MouseHandler{
    Vec2 screen;
    Vec2 world;
    Vec2 prevWorld;

    bool leftDown = false;
    bool rightDown = false;
    bool middleDown = false;
};

class CanvasView : public QWindow{
    Q_OBJECT
public:
    explicit CanvasView(std::shared_ptr<Project> project, QWindow* parent = nullptr);
    ~CanvasView() { if(m_renderTimer) m_renderTimer->stop(); }

private:
    MouseHandler m_mouse;

    GFXDevice m_gfx;
    Camera m_camera;
    std::shared_ptr<Project> m_project;

    void interpDraw(RasterRootNode& targetNode, RGBA color);
    
    // sync
    std::unordered_set<uint64_t> m_deferredSyncKeysMaster;
    size_t syncCompositedOutput(); // returns dirty-tile count, for perf logging
    void syncTileToGPU(uint64_t key);
    void syncTilesImmediate(const std::unordered_set<uint64_t>& keys);
    void renderFrame();
    void reconfigureSurface();
    
    QTimer* m_renderTimer = nullptr;
    bool m_needsRender = false;
    void markDirty() { m_needsRender = true; }
    QTimer* m_resizeDebounce = nullptr;

    // debug
    PerfStats m_perf;
    QElapsedTimer m_perfLogTimer;
    void logPerfIfDue();

protected:
    // events
    void resizeEvent(QResizeEvent* e) override
    {
        Q_UNUSED(e);
        if(!m_gfx.m_initialized) return;
        if(width() <= 0 || height() <= 0) return;

        m_resizeDebounce->start();
    }
    void exposeEvent(QExposeEvent* e) override
    {
        Q_UNUSED(e);
        if(!m_gfx.m_initialized) return;

        if(isExposed())
            reconfigureSurface();
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
            if(m_project->m_activeRaster)
            {
                interpDraw(*m_project->m_activeRaster, {255,255,255,255});
                markDirty();
            }
        }
        else if(m_mouse.rightDown)
        {
            if(m_project->m_activeRaster)
                m_project->m_activeRaster->erasePixel((int)m_mouse.world.x, (int)m_mouse.world.y);
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
};
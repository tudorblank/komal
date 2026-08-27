#pragma once

#include <QWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QMatrix4x4>

#include <QMouseEvent>
#include <QWheelEvent>

#include <webgpu.h>
#include "gfx/gfxdevice.hpp"
#include "raster.hpp"
#include "node/node-base.hpp"
#include "node/node-compositor.hpp"

#include <vector>
#include <algorithm>
#include <cstdint>
#include <deque>

struct MouseHandler{
    Vec2 screen;
    Vec2 world;
    Vec2 prevWorld;

    bool leftDown = false;
    bool rightDown = false;
    bool middleDown = false;
};

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

protected:
    // events
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    MouseHandler m_mouse;

    GFXDevice m_gfx;
    Camera m_camera;

    std::deque<RasterData> m_layers;
    std::vector<std::shared_ptr<RasterRootNode>> m_layerNodes;
    std::shared_ptr<CompositorNode> m_compositor;

    size_t syncCompositedOutput(); // returns dirty-tile count, for perf logging
    void interpDraw(RasterData& inputRaster, RGBA color);
    void renderFrame();
    
    QTimer* m_renderTimer = nullptr;
    bool m_needsRender = false;
    void markDirty() { m_needsRender = true; }

    PerfStats m_perf;
    QElapsedTimer m_perfLogTimer;
    void logPerfIfDue();
};
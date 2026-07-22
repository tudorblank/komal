#pragma once

#include <QWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QMatrix4x4>

#include <QMouseEvent>
#include <QWheelEvent>

#include <webgpu.h>
#include "gfxdevice.h"
#include "raster.h"

#include <vector>
#include <algorithm>
#include <cstdint>

struct MouseHandler
{
    Vec2 screen;
    Vec2 world;
    Vec2 prevWorld;

    bool leftDown = false;
    bool rightDown = false;
    bool middleDown = false;
};

class CanvasWindow : public QWindow
{
    Q_OBJECT
public:
    explicit CanvasWindow(QWindow* parent = nullptr);
    ~CanvasWindow();

protected:
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    MouseHandler m_mouse;

    GFXDevice m_gfx;
    Camera m_camera;

    void syncRasterData(RasterData& raster);
    void interpDraw(RasterData& inputRaster, RGBA color);
    void render();

    RasterData m_drawRaster;
    
    bool m_needsRender = false;
    void markDirty() { m_needsRender = true; }

    QTimer* m_renderTimer = nullptr;
};
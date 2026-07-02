#pragma once
#include "shader.h"
#include "raster.h"
#include "gldevice.h"

// Qt
#include <QWindow>
#include <QTimer>

// GL
#include <QOpenGLContext>

// events
#include <QExposeEvent>
#include <QResizeEvent>
#include <QMouseEvent>

class Area
{
public:
    // world nav (origin = 0,0)
    Vec2 pan = { 0.0f, 0.0f };
    float zoom = 1.0f;

    // screen
    int screenW, screenH;

    Vec2 screenToWorld(float sx, float sy)
    {
        return { (sx - pan.x) / zoom, (sy - pan.y) / zoom };
    }
    Vec2 worldToScreen(float wx, float wy)
    {
        return { wx * zoom + pan.x, wy * zoom + pan.y };
    }

    // hovered pixel in world
    int hoveredWorldX = 0;
    int hoveredWorldY = 0;

    // hovered chunk in chunk grid
    int hoveredChunkX = 0;
    int hoveredChunkY = 0;

    void updateHovered(float x, float y)
    {
        hoveredWorldX = (int)std::floor(x);
        hoveredWorldY = (int)std::floor(y);

        hoveredChunkX = hoveredWorldX >> 6;
        hoveredChunkY = hoveredWorldY >> 6;
    }
};

struct MouseHandler
{
    Vec2 screenPos;
    Vec2 worldPos;

    bool isMoving = false;

    // clicks
    bool leftButtonON = false;
    bool leftButtonOFF = true;

    bool rightButtonON = false;
    bool rightButtonOFF = true;

    bool middleButtonON = false;
    bool middleButtonOFF = true;

    QTimer *stopTimer = nullptr;
};

class CanvasWindow : public QWindow
{
public:
    // constructor
    explicit CanvasWindow(QWindow *parent = nullptr);
    // GL context getter
    QOpenGLContext *context() const { return m_context; }
    // destructor
    ~CanvasWindow();

protected: // events
    // window
    void exposeEvent        (QExposeEvent *) override;
    void resizeEvent        (QResizeEvent *e) override;
    // mouse
    void mouseMoveEvent     (QMouseEvent *e) override;
    void mousePressEvent    (QMouseEvent *e) override;
    void mouseReleaseEvent  (QMouseEvent *e) override;
    void wheelEvent         (QWheelEvent *e) override;

private:
    void initialize();
    void runningFrame();

    // inputs
    MouseHandler m_mouse;

    // logic
    Area m_area;

    // GL window data
    QOpenGLContext *m_context   = nullptr;
    QTimer         *m_timer     = nullptr;

    // GL declarations
    GLDevice m_gl;

    RasterData m_raster;
};
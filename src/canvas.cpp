#include "canvas.h"

#include <QDebug>
#include <QSurfaceFormat>

// constructor
CanvasWindow::CanvasWindow(QWindow *parent)
    : QWindow(parent)
{
    setSurfaceType(QWindow::OpenGLSurface);

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSamples(0);
    fmt.setSwapInterval(1);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(fmt);

    m_area.screenW = 800;
    m_area.screenH = 600;

    m_context = new QOpenGLContext(this);
    m_context->setFormat(fmt);
    m_context->create();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &CanvasWindow::runningFrame);
    m_timer->start(16); // ~60 FPS

    m_mouse.stopTimer = new QTimer(this);
    m_mouse.stopTimer->setSingleShot(true);
    connect(m_mouse.stopTimer, &QTimer::timeout, this, [this]() {
        m_mouse.isMoving = false;
    });
}

// --- events ---
// window
void CanvasWindow::exposeEvent(QExposeEvent *)
{
    if (isExposed())
        runningFrame();
}
void CanvasWindow::resizeEvent(QResizeEvent *e)
{
    m_area.screenW = e->size().width();
    m_area.screenH = e->size().height();

    if(m_gl.initialized && isExposed())
    {
        m_context->makeCurrent(this);

        // recalc projections
        m_gl.setViewport(m_area.screenW, m_area.screenH);
        m_gl.updateProjection(0.0f, m_area.screenW, m_area.screenH, 0.0f);

        // update context
        m_context->doneCurrent();
    }
}
// mouse
void CanvasWindow::mouseMoveEvent(QMouseEvent *e)
{
    QPointF pos = e->position();
    float dx = pos.x() - m_mouse.screenPos.x;
    float dy = pos.y() - m_mouse.screenPos.y;

    if(m_mouse.middleButtonON)
    {
        m_area.pan.x += dx;
        m_area.pan.y += dy;
    }

    // mouse physical position
    m_mouse.screenPos.x = pos.x();
    m_mouse.screenPos.y = pos.y();
    m_mouse.worldPos = m_area.screenToWorld(m_mouse.screenPos.x, m_mouse.screenPos.y);
    m_area.updateHovered(m_mouse.worldPos.x, m_mouse.worldPos.y);

    // track mouse move state
    m_mouse.isMoving = true;
    m_mouse.stopTimer->start(32);
    
    // draw/remove pixels
    if(m_mouse.leftButtonON)
        m_raster.setPixel(m_area.hoveredWorldX, m_area.hoveredWorldY, {255, 255, 255, 255});
    else if(m_mouse.rightButtonON)
        m_raster.erasePixel(m_area.hoveredWorldX, m_area.hoveredWorldY);
}
void CanvasWindow::mousePressEvent(QMouseEvent *e)
{
    if(e->button() == Qt::LeftButton)
    {
        m_mouse.leftButtonON = true;
        m_mouse.leftButtonOFF = false;
    }
    else if(e->button() == Qt::RightButton)
    {
        m_mouse.rightButtonON = true;
        m_mouse.rightButtonOFF = false;
    }
    else if(e->button() == Qt::MiddleButton)
    {
        m_mouse.middleButtonON = true;
        m_mouse.middleButtonOFF = false;
    }
}
void CanvasWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if(e->button() == Qt::LeftButton)
    {
        m_mouse.leftButtonON = false;
        m_mouse.leftButtonOFF = true;
    }
    else if(e->button() == Qt::RightButton)
    {
        m_mouse.rightButtonON = false;
        m_mouse.rightButtonOFF = true;
    }
    else if(e->button() == Qt::MiddleButton)
    {
        m_mouse.middleButtonON = false;
        m_mouse.middleButtonOFF = true;
    }
}
void CanvasWindow::wheelEvent(QWheelEvent *e)
{
    QPointF delta = e->angleDelta();
    
    if(e->modifiers() & Qt::ControlModifier)
    {
        float zoomFactor = (delta.y() > 0) ? 1.1f : 0.9f;
        float newZoom = qBound(0.05f, m_area.zoom * zoomFactor, 50.0f);
        if (newZoom == m_area.zoom) return;

        float actualFactor = newZoom / m_area.zoom;

        float mx = e->position().x();
        float my = e->position().y();

        m_area.pan.x = mx + (m_area.pan.x - mx) * actualFactor;
        m_area.pan.y = my + (m_area.pan.y - my) * actualFactor;

        m_area.zoom = newZoom;
    }
    else
    {
        m_area.pan.y += delta.y() * 0.2f;
        m_area.pan.x += delta.x() * 0.2f;
    }
}

// -- GL --
void CanvasWindow::initialize()
{
    // init
    m_gl.initGL();
    m_gl.setAttributes();

    // buffers
    m_gl.initColBuffers();
    m_gl.initTexBuffers();
    m_gl.initUBO();

    // shaders
    m_gl.initScreenColShader("shaders/color-screen.vert", "shaders/color-screen.frag");
    m_gl.initColShader("shaders/color.vert", "shaders/color.frag");
    m_gl.initTexShader("shaders/texture.vert", "shaders/texture.frag");

    // projection
    m_gl.updateProjection(0.0f, m_area.screenW, m_area.screenH, 0.0f);
    m_gl.setScreenView();
}

void CanvasWindow::runningFrame()
{
    // checks
    if(!isExposed()) return;
    if(m_area.screenW == 0 || m_area.screenH == 0) return;

    m_context->makeCurrent(this);
    if(!m_gl.initialized) CanvasWindow::initialize();

    m_gl.setViewport(m_area.screenW, m_area.screenH);

    // bg color
    m_gl.clear(0.15f, 0.15f, 0.15f, 1.0f);

    // world view
    m_gl.updateWorldView(m_area.pan.x, m_area.pan.y, m_area.zoom);

    // flush dirty chunks to GPU
    m_raster.flushDirtyGL(m_gl);

    // draw all atlas pages
    m_gl.m_tex.shader->Activate();
    m_gl.bindTexVAO();

    for(auto& [key, chunk] : m_raster.m_chunks) // go through each chunk
    {
        m_gl.updateChunkUniforms(chunk);
        m_gl.drawQuad();
    }

    m_gl.unbindTexVAO();

    m_gl.drawScreenBoundsBox(
        m_raster,
        m_area.pan,
        m_area.zoom,
        { 47, 102, 253, 255 });

    qDebug("CPU: %zu KB | GPU: %zu MB | chunks: %zu | pages: %zu",
        m_raster.cpuMemoryBytes() / 1024,
        m_raster.gpuMemoryBytes() / 1024 / 1024,
        m_raster.m_chunks.size(),
        m_raster.m_atlasPages.size());

    // swap
    m_context->swapBuffers(this);
    m_context->doneCurrent();
}

CanvasWindow::~CanvasWindow()
{
    if(m_context)
    {
        m_context->makeCurrent(this);
    }
}
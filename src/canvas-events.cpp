#include "canvas.hpp"

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
        QElapsedTimer t; t.start();
        interpDraw(m_rawRasters[1], {255,255,255,255});
        m_perf.interpDrawNs += t.nsecsElapsed();
        m_perf.interpDrawCalls++;
        markDirty();
    }
    else if(m_mouse.rightDown)
    {
        m_rawRasters[1].erasePixel((int)m_mouse.world.x, (int)m_mouse.world.y);
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

void CanvasWindow::keyPressEvent(QKeyEvent* e)
{
    if(e->key() == Qt::Key_A)
    {
        if(m_rasterNodes.size() >= 2)
        {
            m_masterCompositor->moveLayer(0, 1);
            markDirty();
        }
        qDebug() << "LAYERS MOVED";
    }
    if(e->key() == Qt::Key_Z)
    {
        m_moveNode->setOffset(500, 500);
        markDirty();
    }
    if(e->key() == Qt::Key_B)
    {
        m_moveNode->setOffset(1000, 1000);
        markDirty();
    }
}
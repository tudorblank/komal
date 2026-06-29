#include "canvas.h"

#include <QDebug>
#include <QSurfaceFormat>

// math
void makeProj(float left, float right, float bottom, float top, float* m)
{
    for (int i = 0; i < 16; i++) m[i] = 0.0f; // clean slate

    m[0]  =  2.0f / (right - left);
    m[5]  =  2.0f / (top - bottom);
    m[10] = -1.0f;
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[15] = 1.0f;
}
void makeModel(float x, float y, float w, float h, float* m)
{
    for (int i = 0; i < 16; i++) m[i] = 0.0f; // clean slate

    m[0] = w;
    m[5] = h;
    m[10] = 1.0f;

    m[12] = x;
    m[13] = y;
    m[15] = 1.0f;
}
void makeView(float panX, float panY, float zoom, float* m)
{
    for (int i = 0; i < 16; i++) m[i] = 0.0f;

    m[0]  = zoom;
    m[5]  = zoom;
    m[10] = 1.0f;
    m[12] = panX;
    m[13] = panY;
    m[15] = 1.0f;
}

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
    connect(m_timer, &QTimer::timeout, this, &CanvasWindow::renderFrame);
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
        renderFrame();
}
void CanvasWindow::resizeEvent(QResizeEvent *e)
{
    m_area.screenW = e->size().width();
    m_area.screenH = e->size().height();

    if(m_initialized && isExposed())
    {
        m_context->makeCurrent(this);

        // recalc projections
        glViewport(0, 0, m_area.screenW, m_area.screenH);
        makeProj(0.0f, m_area.screenW, m_area.screenH, 0.0f, m_projection);

        glBindBuffer(GL_UNIFORM_BUFFER, m_UBOmx);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, 16 * sizeof(float), m_projection);

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
void CanvasWindow::initializeGL()
{
    // init
    initializeOpenGLFunctions();
    m_initialized = true;

    // glEnable(GL_BLEND);
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // vert data
    GLfloat colVerts[] = {
    //  x     y
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f
    };
    GLfloat texVerts[] = {
    //  x     y     u     v
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f
    };
    GLuint quadIDX[] = {
        0, 1, 2,
        0, 2, 3
    };

    // gen buffers
    glGenVertexArrays(1, &m_colVAO);
    glGenBuffers(1, &m_colVBO);
    glGenBuffers(1, &m_colEBO);

    glGenVertexArrays(1, &m_texVAO);
    glGenBuffers(1, &m_texVBO);
    glGenBuffers(1, &m_texEBO);

    glGenBuffers(1, &m_UBOmx);

    // bind buffers
    ///// color
    glBindVertexArray(m_colVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_colVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(colVerts), colVerts, GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_colEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIDX), quadIDX, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    glBindVertexArray(0);

    ///// texture
    glBindVertexArray(m_texVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_texVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(texVerts), texVerts, GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_texEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIDX), quadIDX, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);

    ///// UBO
    glBindBuffer(GL_UNIFORM_BUFFER, m_UBOmx);
    glBufferData(GL_UNIFORM_BUFFER, 2 * 16 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_UBOmx);

    // --- shaders ---
    m_colShader = new Shader("shaders/color.vert", "shaders/color.frag");
    m_uColModel     = glGetUniformLocation(m_colShader->ID, "uModel");
    m_uColor        = glGetUniformLocation(m_colShader->ID, "uColor");

    m_texShader = new Shader("shaders/texture.vert", "shaders/texture.frag");
    m_uTexModel     = glGetUniformLocation(m_texShader->ID, "uModel");
    m_uTexture      = glGetUniformLocation(m_texShader->ID, "uTexture");
    m_uUVOffset = glGetUniformLocation(m_texShader->ID, "uUVOffset");
    m_uUVScale  = glGetUniformLocation(m_texShader->ID, "uUVScale");

    GLuint Block0 = glGetUniformBlockIndex(m_colShader->ID, "Matrices");
    glUniformBlockBinding(m_colShader->ID, Block0, 0);
    GLuint Block1 = glGetUniformBlockIndex(m_texShader->ID, "Matrices");
    glUniformBlockBinding(m_texShader->ID, Block1, 0);

    // --- projection ---
    makeProj(0.0f, m_area.screenW, m_area.screenH, 0.0f, m_projection);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 16 * sizeof(float), m_projection);

    makeView(0.0f, 0.0f, 1.0f, m_screenView);

    qDebug("uUVOffset=%d uUVScale=%d", m_uUVOffset, m_uUVScale);
}

void CanvasWindow::renderFrame()
{
    // checks
    if(!isExposed()) return;
    if(m_area.screenW == 0 || m_area.screenH == 0) return;

    m_context->makeCurrent(this);
    if(!m_initialized) initializeGL();

    // viewport + bg color
    glViewport(0, 0, m_area.screenW, m_area.screenH);
    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // view
    makeView(m_area.pan.x, m_area.pan.y, m_area.zoom, m_worldView);
    glBindBuffer(GL_UNIFORM_BUFFER, m_UBOmx);
    glBufferSubData(GL_UNIFORM_BUFFER, 16 * sizeof(float), 16 * sizeof(float), m_worldView);

    // flush dirty chunks to GPU
    m_raster.flushDirty();
    // draw all atlas pages
    m_texShader->Activate();
    glBindVertexArray(m_texVAO);

    for(auto& [key, chunk] : m_raster.m_chunks) // go through each chunk
    {
        Atlas& atlas = m_raster.getAtlasForChunk(chunk);

        // world position of this chunk
        float wx = chunk.cPosX * Chunk::SIZE;
        float wy = chunk.cPosY * Chunk::SIZE;

        makeModel(wx, wy, Chunk::SIZE, Chunk::SIZE, m_model);
        glUniformMatrix4fv(m_uTexModel, 1, GL_FALSE, m_model);

        float uvOffsetX = (float)chunk.atlasSlotX / Atlas::SLOTS_PER_ROW;
        float uvOffsetY = (float)chunk.atlasSlotY / Atlas::SLOTS_PER_ROW;
        float uvScale   = 1.0f / Atlas::SLOTS_PER_ROW;

        glUniform2f(m_uUVOffset, uvOffsetX, uvOffsetY);
        glUniform1f(m_uUVScale, uvScale);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas.texID);
        glUniform1i(m_uTexture, 0);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    }

    glBindVertexArray(0);

    // qDebug("pixel: (%d, %d) | chunk: (%d, %d)", m_area.hoveredWorldX, m_area.hoveredWorldY, m_area.hoveredChunkX, m_area.hoveredChunkY);
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
    if(m_context && m_initialized)
    {
        m_context->makeCurrent(this);

        glDeleteVertexArrays(1, &m_colVAO);
        glDeleteBuffers(1, &m_colVBO);
        glDeleteBuffers(1, &m_colEBO);

        glDeleteVertexArrays(1, &m_texVAO);
        glDeleteBuffers(1, &m_texVBO);
        glDeleteBuffers(1, &m_texEBO);

        glDeleteBuffers(1, &m_UBOmx);

        m_context->doneCurrent();
    }
    
    delete m_colShader;
    delete m_texShader;
}

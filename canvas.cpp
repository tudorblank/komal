#include "canvas.h"

#include <QDebug>
#include <QSurfaceFormat>

// raster data
RasterData::RasterData()
{
    localX = 0;
    localY = 0;
    width = 0;
    height = 0;
    texID = 0;
}
RasterData::~RasterData()
{
    if(texID){
        glDeleteTextures(1, &texID);
        texID = 0;
    }
}

void RasterData::setPixel(int x, int y, RGBA color)
{
    m_pixels[key(x, y)] = color;
}
void RasterData::removePixel(int x, int y)
{
    m_pixels.erase(key(x,y));
}
void RasterData::buildTexture()
{
    if(m_pixels.empty()) return;

    // bounding box
    int minX = INT_MAX, minY = INT_MAX;
    int maxX = INT_MIN, maxY = INT_MIN;

    for(auto& [k, rgba] : m_pixels)
    {
        // key = k; rgba = value
        int x = (int32_t)(k >> 32); // upper 32 bits
        int y = (int32_t)(k & 0xFFFFFFFF); // bottom 32 bits
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }

    // update local pos+size
    localX = minX;
    localY = minY;
    width  = maxX - minX + 1;
    height = maxY - minY + 1;

    // build pixel buffer
    std::vector<uint8_t> buf(width * height * 4, 0);
    for(auto& [k, rgba] : m_pixels)
    {
        int x = (int32_t)(k >> 32);
        int y = (int32_t)(k & 0xFFFFFFFF);
        int i = ((y - minY) * width + (x - minX)) * 4;
        buf[i+0] = rgba.r;
        buf[i+1] = rgba.g;
        buf[i+2] = rgba.b;
        buf[i+3] = rgba.a;
    }

    // upload to GL
    if(!texID) glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

// math shit
void ortho(float left, float right, float bottom, float top, float* m)
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
    m_area.globalW = e->size().width();
    m_area.globalH = e->size().height();

    if(m_initialized && isExposed())
    {
        m_context->makeCurrent(this);

        // recalc projections
        glViewport(0, 0, m_area.globalW, m_area.globalH);
        ortho(0.0f, m_area.globalW, m_area.globalH, 0.0f, m_projection);

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
    float dx = pos.x() - m_mouse.globalPos.x;
    float dy = pos.y() - m_mouse.globalPos.y;

    if(m_mouse.middleButtonON)
    {
        m_area.pan.x += dx;
        m_area.pan.y += dy;
    }

    m_mouse.globalPos.x = pos.x();
    m_mouse.globalPos.y = pos.y();
    m_mouse.localPos = m_area.globalToLocal(m_mouse.globalPos.x, m_mouse.globalPos.y);

    // track mouse move state
    m_mouse.isMoving = true;
    m_mouse.stopTimer->start(32);

    // draw/remove pixels
    if(m_mouse.leftButtonON)
    {
        int px = (int)std::floor(m_mouse.localPos.x);
        int py = (int)std::floor(m_mouse.localPos.y);
        m_raster.setPixel(px, py, {255, 255, 255, 255});
        m_rasterDirty = true;
    }
    else if(m_mouse.rightButtonON)
    {
        int px = (int)std::floor(m_mouse.localPos.x);
        int py = (int)std::floor(m_mouse.localPos.y);
        m_raster.removePixel(px, py);
        m_rasterDirty = true;
    }
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

    GLuint Block0 = glGetUniformBlockIndex(m_colShader->ID, "Matrices");
    glUniformBlockBinding(m_colShader->ID, Block0, 0);
    GLuint Block1 = glGetUniformBlockIndex(m_texShader->ID, "Matrices");
    glUniformBlockBinding(m_texShader->ID, Block1, 0);

    // --- projection ---
    ortho(0.0f, m_area.globalW, m_area.globalH, 0.0f, m_projection);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 16 * sizeof(float), m_projection);

    // --- raster ---
    m_raster.setPixel(0,  0,  {255, 0,   0,   255}); // red
    m_raster.setPixel(20, 20, {0,   255, 0,   255}); // green
    m_raster.setPixel(10, 5,  {0,   0,   255, 255}); // blue
    m_raster.buildTexture();
}

void CanvasWindow::renderFrame()
{
    // checks
    if(!isExposed()) return;
    m_context->makeCurrent(this);
    if(!m_initialized) initializeGL();

    if(m_rasterDirty)
    {
        m_raster.buildTexture();
        m_rasterDirty = false;
    }

    // viewport + bg color
    glViewport(0, 0, m_area.globalW, m_area.globalH);
    glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // view
    makeView(m_area.pan.x, m_area.pan.y, m_area.zoom, m_view);
    glBindBuffer(GL_UNIFORM_BUFFER, m_UBOmx);
    glBufferSubData(GL_UNIFORM_BUFFER, 16 * sizeof(float), 16 * sizeof(float), m_view);

    /*
    // uColor shape (0, 0)
    m_colShader->Activate();
    glBindVertexArray(m_colVAO);

    makeModel(m_area.localOrigin.x, m_area.localOrigin.y, 200.0f, 150.0f, m_model);
    glUniformMatrix4fv(m_uColModel, 1, GL_FALSE, m_model);

    glUniform3f(m_uColor, 1.0f, 0.5f, 0.0f);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);
    */

    // uTexture shape (220, 0)
    m_texShader->Activate();
    glBindVertexArray(m_texVAO);

    makeModel((float)m_raster.localX, (float)m_raster.localY,
            (float)m_raster.width,  (float)m_raster.height, m_model);
    glUniformMatrix4fv(m_uTexModel, 1, GL_FALSE, m_model);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_raster.texID);
    glUniform1i(m_uTexture, 0);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    qDebug("GLOBAL: %.1f, %.1f", m_mouse.globalPos.x, m_mouse.globalPos.y);
    qDebug("LOCAL: %.1f, %.1f", m_mouse.localPos.x, m_mouse.localPos.y);

    m_context->swapBuffers(this);
    m_context->doneCurrent();
}

CanvasWindow::~CanvasWindow()
{
    delete m_colShader;
    delete m_texShader;
}

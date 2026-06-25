#pragma once

#include "shader.h"

// Qt
#include <QWindow>
#include <QTimer>

// GL
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_3_Core>

// events
#include <QExposeEvent>
#include <QResizeEvent>
#include <QMouseEvent>

struct Vec2
{
    float x, y;
};

class Area
{
public:
    //global
    int globalW, globalH;

    //local (origin = 0,0)
    Vec2 pan = { 0.0f, 0.0f };
    float zoom = 1.0f;

    Vec2 globalToLocal(float gx, float gy)
    {
        return { (gx - pan.x) / zoom, (gy - pan.y) / zoom };
    }
    Vec2 localToGlobal(float lx, float ly)
    {
        return { lx * zoom + pan.x, ly * zoom + pan.y };
    }
};

class RasterData
{
public:
    struct RGBA{
        uint8_t r, g, b, a;
    };

    RasterData();
    ~RasterData();

    void setPixel(int x, int y, RGBA color);
    void removePixel(int x, int y);
    void buildTexture();

    int localX, localY;
    int width, height;

    GLuint texID = 0;

private:
    std::unordered_map<uint64_t, RGBA> m_pixels;

    uint64_t key(int x, int y)
    {
        // (<<) move x to upper 32 bits out of the 64;
        // (|) glue x and y (both pairs of 32 bits) into one 64
        return ((uint64_t)(int32_t)x << 32) | (uint32_t)y;
    }

};

struct mouseHandler
{
    Vec2 globalPos;
    Vec2 localPos;

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

void ortho      (float left, float right, float bottom, float top, float* m);
void makeModel  (float x, float y, float w, float h, float* m);
void makeView   (float panX, float panY, float zoom, float* m);

class CanvasWindow : public QWindow, protected QOpenGLFunctions_3_3_Core
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
    void initializeGL();
    void renderFrame();

    // inputs
    mouseHandler m_mouse;

    // logic
    Area m_area;

    // GL window data
    QOpenGLContext *m_context   = nullptr;
    QTimer         *m_timer     = nullptr;
    bool            m_initialized = false;

    // GL declarations

    // color
    GLuint m_colVAO, m_colVBO, m_colEBO;
    GLint m_uColor;
    Shader *m_colShader = nullptr;
    GLint m_uColModel;

    // texture
    GLuint m_texVAO, m_texVBO, m_texEBO;
    GLint m_uTexture;
    Shader *m_texShader = nullptr;
    GLint m_uTexModel;
    
    // UBO + mx's
    GLuint m_UBOmx;
    float m_projection[16];
    float m_model[16];
    float m_view[16];

    RasterData m_raster;
    bool m_rasterDirty = false;
};
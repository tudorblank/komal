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
    //world nav (origin = 0,0)
    Vec2 pan = { 0.0f, 0.0f };
    float zoom = 1.0f;

    //screen
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

struct RGBA
{
    uint8_t r, g, b, a;
};

class Chunk
{
public:
    static constexpr int SIZE = 64;
    RGBA data[SIZE * SIZE] = {};
    bool dirty = false;

    // chunk grid location
    int cPosX, cPosY;
    
    // atlas location
    int atlasPage = -1;  // which atlas texture
    int atlasSlotX = -1; // slot col
    int atlasSlotY = -1; // slot row

    RGBA& pixel(int lx, int ly) // direct access to pixel
    {
        return data[ly * SIZE + lx];
    }
};

class Atlas // GL texture pool that holds chunks
{
public:
    bool initialized = false;
    Atlas() {}
    // no copying
    Atlas(const Atlas&) = delete;
    Atlas& operator=(const Atlas&) = delete;

    // moving is fine
    Atlas(Atlas&& o) noexcept : texID(o.texID), usedSlots(o.usedSlots)
    {
        o.texID = 0; // prevent the moved-from object from deleting the texture
    }
    Atlas& operator=(Atlas&& o) noexcept
    {
        if(this != &o)
        {
            if(texID) glDeleteTextures(1, &texID);
            texID = o.texID;
            usedSlots = o.usedSlots;
            o.texID = 0;
        }
        return *this;
    }

    static constexpr int PAGE_SIZE = 4096;
    static constexpr int SLOTS_PER_ROW  = PAGE_SIZE / Chunk::SIZE; // 64
    static constexpr int SLOTS_TOTAL    = SLOTS_PER_ROW * SLOTS_PER_ROW; // 4096

    GLuint texID = 0;
    int usedSlots = 0;

    void init()
    {
        // gen atlas tex
        glGenTextures(1, &texID);
        // bind atlas tex
        glBindTexture(GL_TEXTURE_2D, texID);
        // atlas tex filters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        // allocate GPU storage for tex
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, PAGE_SIZE, PAGE_SIZE,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        // unbind atlas tex
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void assignSlot(Chunk& c, int pageIndex)
    {
        c.atlasPage = pageIndex;
        c.atlasSlotX = usedSlots % SLOTS_PER_ROW; // [0] = col 0; [64] = col 0
        c.atlasSlotY = usedSlots / SLOTS_PER_ROW; // [<64] = row 0; 64 = row 1
        usedSlots++;
    }

    bool isFull() { return usedSlots >= SLOTS_TOTAL; }

    void uploadChunk(Chunk& c) // to GL texture
    {
        // bind atlas tex
        glBindTexture(GL_TEXTURE_2D, texID);
        // edit PART of the atlas tex
        glTexSubImage2D(GL_TEXTURE_2D, 0,
                        c.atlasSlotX * Chunk::SIZE, // x coord in atlas
                        c.atlasSlotY * Chunk::SIZE, // y coord in atlas
                        Chunk::SIZE, Chunk::SIZE,   // 64 x 64
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        c.data);
        // unbind atlas tex
        glBindTexture(GL_TEXTURE_2D, 0);
        c.dirty = false;
    }

    ~Atlas()
    {
        if(texID) glDeleteTextures(1, &texID); // GL cleanup
    }
};

class RasterData
{
public:
    RasterData()  {}
    ~RasterData() {}

    std::unordered_map<uint64_t, Chunk> m_chunks;
    std::vector<Atlas> m_atlasPages;

    uint64_t chunkKey(int chunkGridX, int chunkGridY)
    {
        return ((uint64_t)(int32_t)chunkGridX << 32) | (uint32_t)chunkGridY;
    }

    // ---chunk data structure relationship---

    bool hasChunk(int chunkGridX, int chunkGridY)
    {
        return m_chunks.count(chunkKey(chunkGridX, chunkGridY)) > 0;
    }

    Chunk& createChunk(int chunkGridX, int chunkGridY)
    {
        if(hasChunk(chunkGridX, chunkGridY))
            return m_chunks[chunkKey(chunkGridX, chunkGridY)];
        
        // find atlas page with space, or create new one
        if(m_atlasPages.empty() || m_atlasPages.back().isFull())
        {
            m_atlasPages.emplace_back();
        }

        Chunk c;
        // update chunk interal position on chunk grid
        c.cPosX = chunkGridX;
        c.cPosY = chunkGridY;

        int pageIndex = m_atlasPages.size() - 1;
        m_atlasPages[pageIndex].assignSlot(c, pageIndex);

        m_chunks[chunkKey(chunkGridX, chunkGridY)] = c;
        return m_chunks[chunkKey(chunkGridX, chunkGridY)];
    }

    Chunk& accessChunk(int chunkGridX, int chunkGridY)
    {
        if(!hasChunk(chunkGridX, chunkGridY))
            createChunk(chunkGridX, chunkGridY);
        
        return m_chunks[chunkKey(chunkGridX, chunkGridY)];
    }

    void setPixel(int worldX, int worldY, RGBA color)
    {
        int chunkGridX = worldX >> 6;
        int chunkGridY = worldY >> 6;
        int pxLocalX = worldX & 63;
        int pxLocalY = worldY & 63;

        Chunk& tempChunk = accessChunk(chunkGridX, chunkGridY);
        tempChunk.pixel(pxLocalX, pxLocalY) = color;
        tempChunk.dirty = true;
    }

    void erasePixel(int worldX, int worldY)
    {
        int chunkGridX = worldX >> 6;
        int chunkGridY = worldY >> 6;

        if(!hasChunk(chunkGridX, chunkGridY)) return; // chunk doesn't exist, nothing to erase

        int pxLocalX = worldX & 63;
        int pxLocalY = worldY & 63;

        Chunk& tempChunk = m_chunks[chunkKey(chunkGridX, chunkGridY)];
        tempChunk.pixel(pxLocalX, pxLocalY) = {0, 0, 0, 0};
        tempChunk.dirty = true;
    }

    // ---atlas stuff---

    void flushDirty()
{
    for(auto& [key, chunk] : m_chunks)
    {
        Atlas& atlas = m_atlasPages[chunk.atlasPage];

        if(!atlas.initialized)
        {
            atlas.init();
            atlas.initialized = true;
        }

        if(chunk.dirty)
            atlas.uploadChunk(chunk);
    }
}

    Atlas& getAtlasForChunk(Chunk& inputChunk)
    {
        return m_atlasPages[inputChunk.atlasPage];
    }

    size_t cpuMemoryBytes() // each chunk is 64*64*4 = 16KB
    { return m_chunks.size() * sizeof(Chunk); }

    size_t gpuMemoryBytes() // each atlas page is 4096*4096*4 bytes = 64MB
    { return m_atlasPages.size() * Atlas::PAGE_SIZE * Atlas::PAGE_SIZE * 4; }
};

struct mouseHandler
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

void makeProj   (float left, float right, float bottom, float top, float* m);
void makeModel  (float x, float y, float w, float h, float* m);
void makeView   (float panX, float panY, float zoom, float* m);

// class GLDevice
// {
//      to be implemented...
// };

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
    GLint m_uUVOffset;
    GLint m_uUVScale;
    
    // UBO + mx's
    GLuint m_UBOmx;
    float m_projection[16];
    float m_model[16];
    float m_worldView[16];
    float m_screenView[16]; // for bounding box view

    RasterData m_raster;
};
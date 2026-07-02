#include "gldevice.h"

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

// GLdevice
void GLDevice::initGL()
{
    // init GL functions
    initializeOpenGLFunctions();
    initialized = true;
}

// init buffers
void GLDevice::initColBuffers()
{
    //// vertex data
    GLfloat verts[] = {
    //  x     y
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f
    };
    GLuint quadIDX[] = {
        0, 1, 2,
        0, 2, 3
    };
    
    //// gen buffers
    glGenVertexArrays(1, &m_col.VAO);
    glGenBuffers(1, &m_col.VBO);
    glGenBuffers(1, &m_col.EBO);

    //// bind buffers
    // VAO
    glBindVertexArray(m_col.VAO);

    // VBO
    glBindBuffer(GL_ARRAY_BUFFER, m_col.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    // EBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_col.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIDX), quadIDX, GL_STATIC_DRAW);

    // link attrib
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    //// unbind buffers
    glBindVertexArray(0);                     // VAO
    glBindBuffer(GL_ARRAY_BUFFER, 0);         // VBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); // EBO
}
void GLDevice::initTexBuffers()
{
    //// vertex data
    GLfloat verts[] = {
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

    //// gen buffers
    glGenVertexArrays(1, &m_tex.VAO);
    glGenBuffers(1, &m_tex.VBO);
    glGenBuffers(1, &m_tex.EBO);

    //// bind buffers
    // VAO
    glBindVertexArray(m_tex.VAO);

    // VBO
    glBindBuffer(GL_ARRAY_BUFFER, m_tex.VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    // EBO
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_tex.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIDX), quadIDX, GL_STATIC_DRAW);

    // link attribs
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    //// unbind buffers
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
void GLDevice::initUBO()
{
    // gen buffer
    glGenBuffers(1, &m_UBO);

    // bind buffer
    glBindBuffer(GL_UNIFORM_BUFFER, m_UBO);
    glBufferData(GL_UNIFORM_BUFFER, 2 * 16 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_UBO);
    
    // unbind buffer
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}
// init shaders
void GLDevice::initScreenColShader(const char* vertPath, const char* fragPath)
{
    m_screenCol.shader = new Shader(vertPath, fragPath);
    m_screenCol.uModel      = glGetUniformLocation(m_screenCol.shader->ID, "uModel");
    m_screenCol.uColor      = glGetUniformLocation(m_screenCol.shader->ID, "uColor");
    m_screenCol.uView       = glGetUniformLocation(m_screenCol.shader->ID, "uView");

    GLuint screenColorBlock = glGetUniformBlockIndex(m_screenCol.shader->ID, "Matrices");
    glUniformBlockBinding(m_screenCol.shader->ID, screenColorBlock, 0);
}
void GLDevice::initColShader(const char* vertPath, const char* fragPath)
{
    m_col.shader = new Shader(vertPath, fragPath);
    m_col.uModel      = glGetUniformLocation(m_col.shader->ID, "uModel");
    m_col.uColor      = glGetUniformLocation(m_col.shader->ID, "uColor");

    GLuint colorBlock = glGetUniformBlockIndex(m_col.shader->ID, "Matrices");
    glUniformBlockBinding(m_col.shader->ID, colorBlock, 0);
}
void GLDevice::initTexShader(const char* vertPath, const char* fragPath)
{
    m_tex.shader = new Shader(vertPath, fragPath);
    m_tex.uModel      = glGetUniformLocation(m_tex.shader->ID, "uModel");
    m_tex.uTexture    = glGetUniformLocation(m_tex.shader->ID, "uTexture");

    m_tex.uUVOffset   = glGetUniformLocation(m_tex.shader->ID, "uUVOffset");
    m_tex.uUVScale    = glGetUniformLocation(m_tex.shader->ID, "uUVScale");

    GLuint textureBlock = glGetUniformBlockIndex(m_tex.shader->ID, "Matrices");
    glUniformBlockBinding(m_tex.shader->ID, textureBlock, 0);
}

// project + view
void GLDevice::updateProjection(float left, float right, float bottom, float top)
{
    makeProj(left, right, bottom, top, m_projection);
    bindUBO();
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 16 * sizeof(float), m_projection);
    unbindUBO();
}
void GLDevice::updateWorldView(float panX, float panY, float zoom)
{
    makeView(panX, panY, zoom, m_worldView);

    bindUBO();
    glBufferSubData(GL_UNIFORM_BUFFER, 16 * sizeof(float), 16 * sizeof(float), m_worldView);
    unbindUBO();
}
void GLDevice::setScreenView()
{
    makeView(0.0f, 0.0f, 1.0f, m_screenView);

    m_screenCol.shader->Activate();
    glUniformMatrix4fv(m_screenCol.uView, 1, GL_FALSE, m_screenView);
}

// raster
void GLDevice::initAtlasTexture(Atlas& atlas)
{
    glGenTextures(1, &atlas.texID);
    // bind atlas tex
    glBindTexture(GL_TEXTURE_2D, atlas.texID);
    // atlas tex filters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // allocate GPU storage for tex
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Atlas::PAGE_SIZE, Atlas::PAGE_SIZE,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    // unbind atlas tex
    glBindTexture(GL_TEXTURE_2D, 0);

    atlas.initialized = true;
}
void GLDevice::uploadChunkToAtlas(Chunk& chunk)
{
    Atlas& atlas = chunk.getAtlas();

    glBindTexture(GL_TEXTURE_2D, atlas.texID);

    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    chunk.atlasSlotX * Chunk::SIZE,
                    chunk.atlasSlotY * Chunk::SIZE,
                    Chunk::SIZE, Chunk::SIZE,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    chunk.data);

    glBindTexture(GL_TEXTURE_2D, 0);
    chunk.dirty = false;
}
void GLDevice::updateChunkUniforms(Chunk& chunk)
{
    Atlas& atlas = chunk.getAtlas();

    // world position
    float wx = chunk.cPosX * Chunk::SIZE;
    float wy = chunk.cPosY * Chunk::SIZE;
    // calc UV offset + scale
    float tempUVx = (float)chunk.atlasSlotX / Atlas::SLOTS_PER_ROW;
    float tempUVy = (float)chunk.atlasSlotY / Atlas::SLOTS_PER_ROW;
    float tempUVscale   = 1.0f / Atlas::SLOTS_PER_ROW;

    makeModel(wx, wy, Chunk::SIZE, Chunk::SIZE, m_model);
    glUniformMatrix4fv(m_tex.uModel, 1, GL_FALSE, m_model);

    glUniform2f(m_tex.uUVOffset, tempUVx, tempUVy);
    glUniform1f(m_tex.uUVScale, tempUVscale);

    GLDevice::bindTexture(atlas.texID);
    glUniform1i(m_tex.uTexture, 0);
}
void GLDevice::drawScreenBoundsBox(RasterData& inputRaster, Vec2 pan, float zoom, RGBA color)
{
    BoundsI& rasterBounds = inputRaster.returnPixelBounds();

    if(!rasterBounds.valid)
        return;

    Vec2 topLeft = {
        rasterBounds.minX * zoom + pan.x,
        rasterBounds.minY * zoom + pan.y
    };

    Vec2 bottomRight = {
        (rasterBounds.maxX + 1) * zoom + pan.x,
        (rasterBounds.maxY + 1) * zoom + pan.y
    };

    makeModel(
        topLeft.x, 
        topLeft.y, 
        bottomRight.x - topLeft.x, 
        bottomRight.y - topLeft.y, 
        m_model);

    m_screenCol.shader->Activate();
    glUniformMatrix4fv(m_screenCol.uModel, 1, GL_FALSE, m_model);
    glUniform4f(m_screenCol.uColor,
        color.r / 255.0f, color.g / 255.0f,
        color.b / 255.0f, color.a / 255.0f);

    bindColVAO();
    glDrawArrays(GL_LINE_LOOP, 0, 4);
    unbindColVAO();
}

// destructor
GLDevice::~GLDevice()
{   
    // del shaders
    if(m_col.shader) { m_col.shader->Delete(); delete m_col.shader; m_col.shader = nullptr; } 
    if(m_tex.shader) { m_tex.shader->Delete(); delete m_tex.shader; m_tex.shader = nullptr; }
    if(m_screenCol.shader) { m_screenCol.shader->Delete(); delete m_screenCol.shader; m_screenCol.shader = nullptr; }

    // del buffers
    glDeleteVertexArrays(1, &m_col.VAO);
    glDeleteBuffers(1, &m_col.VBO);
    glDeleteBuffers(1, &m_col.EBO);

    glDeleteVertexArrays(1, &m_tex.VAO);
    glDeleteBuffers(1, &m_tex.VBO);
    glDeleteBuffers(1, &m_tex.EBO);

    glDeleteBuffers(1, &m_UBO);
}
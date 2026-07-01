#pragma once
#include "shader.h"

#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLContext>

class Chunk;
class Atlas;
class RasterData;

void makeProj(float left, float right, float bottom, float top, float* m);
void makeModel(float x, float y, float w, float h, float* m);
void makeView(float panX, float panY, float zoom, float* m);

class GLDevice : protected QOpenGLFunctions_3_3_Core
{
public:
    GLDevice() {}

    bool initialized = false;
    void initGL();

    // regulars
    void setAttributes()
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    void setViewport(int w, int h) { glViewport(0, 0, w, h); }
    void clear(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    void drawQuad() { glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0); }

    // GL objects
    struct ScreenColorObject {
        // shader
        Shader *shader = nullptr;
        // uniforms
        GLint uModel, uColor, uView;
    };
    struct ColorObject {
        // buffers
        GLuint VAO, VBO, EBO;
        // shader
        Shader *shader = nullptr;
        // uniforms
        GLint uModel, uColor;
    };
    struct TextureObject {
        // buffers
        GLuint VAO, VBO, EBO;
        // shader
        Shader *shader = nullptr;
        // uniforms
        GLint uModel, uTexture, uUVOffset, uUVScale;
    };
    ColorObject m_col;
    TextureObject m_tex;
    ScreenColorObject m_screenCol;
    GLuint m_UBO;

    float m_projection[16];
    float m_model[16];
    float m_worldView[16];
    float m_screenView[16];

    // init
    void initColBuffers();
    void initTexBuffers();
    void initUBO();

    void initScreenColShader(const char* vertPath, const char* fragPath);
    void initColShader(const char* vertPath, const char* fragPath);
    void initTexShader(const char* vertPath, const char* fragPath);

    // bind / unbind / delete
    void bindColVAO() { glBindVertexArray(m_col.VAO); }
    void unbindColVAO() { glBindVertexArray(0); }
    
    void bindTexVAO() { glBindVertexArray(m_tex.VAO); }
    void unbindTexVAO() { glBindVertexArray(0); }

    void bindUBO() { glBindBuffer(GL_UNIFORM_BUFFER, m_UBO); }
    void unbindUBO() { glBindBuffer(GL_UNIFORM_BUFFER, 0); }

    void bindTexture(GLuint texID)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texID);
    }
    void unbindTexture()
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    static void deleteTexture(GLuint texID)
    {
        
        if(auto* ctx = QOpenGLContext::currentContext())
            ctx->functions()->glDeleteTextures(1, &texID);
    }

    // project + view
    void updateProjection(float left, float right, float bottom, float top);
    void updateWorldView(float panX, float panY, float zoom);
    void setScreenView();

    // raster
    void initAtlasTexture(Atlas& atlas);
    void uploadChunkToAtlas(Chunk& chunk);
    void updateChunkUniforms(Chunk& chunk);

    ~GLDevice();
};
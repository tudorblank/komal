#ifndef SHADER_CLASS_H
#define SHADER_CLASS_H

#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_3_Core>
#include <string>

class Shader : protected QOpenGLFunctions_3_3_Core
{
public:
    GLuint ID = 0;
    Shader() = default;
    Shader(const char* vertexFile, const char* fragmentFile);
    void Activate();
    void Delete();
};

#endif
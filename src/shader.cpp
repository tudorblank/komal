#include "shader.h"
#include <QOpenGLContext>
#include <fstream>
#include <sstream>
#include <iostream>

static std::string readFile(const char* filename)
{
    std::ifstream f(filename);
    if (!f) throw std::runtime_error(std::string("Cannot open: ") + filename);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

Shader::Shader(const char* vertexFile, const char* fragmentFile)
{
    initializeOpenGLFunctions();

    std::string vertSrc = readFile(vertexFile);
    std::string fragSrc = readFile(fragmentFile);
    const char* vSrc = vertSrc.c_str();
    const char* fSrc = fragSrc.c_str();

    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vSrc, nullptr);
    glCompileShader(vert);
    GLint ok;
    glGetShaderiv(vert, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(vert, 512, nullptr, log); std::cerr << "Vert: " << log << "\n"; }

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fSrc, nullptr);
    glCompileShader(frag);
    glGetShaderiv(frag, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(frag, 512, nullptr, log); std::cerr << "Frag: " << log << "\n"; }

    ID = glCreateProgram();
    glAttachShader(ID, vert);
    glAttachShader(ID, frag);
    glLinkProgram(ID);
    glGetProgramiv(ID, GL_LINK_STATUS, &ok);
    if (!ok) { char log[512]; glGetProgramInfoLog(ID, 512, nullptr, log); std::cerr << "Link: " << log << "\n"; }

    glDeleteShader(vert);
    glDeleteShader(frag);
}

void Shader::Activate() { glUseProgram(ID); }
void Shader::Delete()   { glDeleteProgram(ID); }
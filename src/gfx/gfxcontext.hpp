#pragma once
#include <webgpu.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>

struct GFXContext
{
    WGPUInstance instance = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
};

inline std::string readFile(const char* filename)
{
    std::ifstream f(filename);
    if(!f) throw std::runtime_error(std::string("Cannot open: ") + filename);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static WGPUStringView sv(const char* s) {
    return WGPUStringView{ s, strlen(s) };
}
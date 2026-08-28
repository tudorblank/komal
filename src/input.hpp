#pragma once

#include "raster.hpp"

struct MouseHandler{
    Vec2 screen;
    Vec2 world;
    Vec2 prevWorld;

    bool leftDown = false;
    bool rightDown = false;
    bool middleDown = false;
};
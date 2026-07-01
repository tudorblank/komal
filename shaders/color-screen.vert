#version 330 core
layout (location = 0) in vec2 aPos;

layout(std140) uniform Matrices {
    mat4 uProjection;
};

uniform mat4 uModel;
uniform mat4 uView;

void main()
{
    gl_Position = uProjection * uView * uModel * vec4(aPos, 0.0, 1.0);
}
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;

layout(std140) uniform Matrices {
    mat4 uProjection;
    mat4 uView;
};

uniform mat4 uModel;
uniform vec2 uUVOffset;
uniform float uUVScale;

out vec2 vUV;

void main()
{
    vUV = uUVOffset + aUV * uUVScale;
    gl_Position = uProjection * uView * uModel * vec4(aPos, 0.0, 1.0);
}
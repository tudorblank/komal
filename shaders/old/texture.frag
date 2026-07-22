#version 330 core
in vec2 vUV;

uniform sampler2D uTexture;
uniform float uOpacity;

out vec4 FragColor;

void main()
{
    vec4 texColor = texture(uTexture, vUV);
    FragColor = vec4(texColor.rgb, texColor.a * uOpacity);
}

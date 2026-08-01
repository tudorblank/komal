struct ScreenCamera {
    proj : mat4x4f,
};
struct LineColor {
    color : vec4f,
};

@group(0) @binding(0) var<uniform> uScreenCam : ScreenCamera;
@group(1) @binding(0) var<uniform> uColor : LineColor;

@vertex
fn vs_main(@location(0) pos : vec2f) -> @builtin(position) vec4f {
    return uScreenCam.proj * vec4f(pos, 0.0, 1.0);
}

@fragment
fn fs_main() -> @location(0) vec4f {
    return uColor.color;
}
// Solid-colour rectangles in world space.

struct VertexOut {
    @builtin(position) clipPos : vec4f,
};

struct Camera {
    viewProj : mat4x4f,
};
struct ObjectData {
    model : mat4x4f,   // unit quad centred on the origin -> world rectangle
    color : vec4f,
};

@group(0) @binding(0) var<uniform> uCamera : Camera;
@group(1) @binding(0) var<uniform> uObject : ObjectData;

const kCorners = array<vec2f, 4>(vec2f(-0.5, -0.5), vec2f(0.5, -0.5), vec2f(-0.5, 0.5), vec2f(0.5, 0.5));

@vertex
fn vs_main(@builtin(vertex_index) vi : u32) -> VertexOut {
    var out : VertexOut;
    out.clipPos = uCamera.viewProj * uObject.model * vec4f(kCorners[vi], 0.0, 1.0);
    return out;
}

@fragment
fn fs_main() -> @location(0) vec4f {
    return uObject.color;
}

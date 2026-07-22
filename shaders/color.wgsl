struct VertexIn {
    @location(0) pos : vec2f,
};

struct VertexOut {
    @builtin(position) clipPos : vec4f,
};

struct Camera {
    viewProj : mat4x4f,
};
struct ObjectData {
    model : mat4x4f,
    color : vec4f,
};

@group(0) @binding(0) var<uniform> uCamera : Camera;
@group(1) @binding(0) var<uniform> uObject : ObjectData;

@vertex
fn vs_main(in : VertexIn) -> VertexOut {
    var out : VertexOut;
    out.clipPos = uCamera.viewProj * uObject.model * vec4f(in.pos, 0.0, 1.0);
    return out;
}

@fragment
fn fs_main() -> @location(0) vec4f {
    return uObject.color;
}
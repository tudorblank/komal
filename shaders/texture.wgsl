struct VertexIn {
    @location(0) pos : vec2f,
    @location(1) uv  : vec2f,
};

struct InstanceIn {
    @location(2) center   : vec2f,
    @location(3) scale    : vec2f,
    @location(4) uvOffset : vec2f,
    @location(5) uvScale  : vec2f,
    @location(6) opacity  : f32,
};

struct VertexOut {
    @builtin(position) clipPos : vec4f,
    @location(0) uv : vec2f,
};

struct Camera {
    viewProj : mat4x4f,
};

@group(0) @binding(0) var<uniform> uCamera : Camera;
@group(1) @binding(0) var uTexture : texture_2d<f32>;
@group(1) @binding(1) var uSampler : sampler;

@vertex
fn vs_main(in : VertexIn, inst : InstanceIn) -> VertexOut {
    var out : VertexOut;

    let worldPos = in.pos * inst.scale + inst.center;
    out.clipPos = uCamera.viewProj * vec4f(worldPos, 0.0, 1.0);

    out.uv = in.uv * inst.uvScale + inst.uvOffset;

    return out;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4f {
    return textureSample(uTexture, uSampler, in.uv);
}
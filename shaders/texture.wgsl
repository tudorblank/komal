struct VertexIn {
    @location(0) pos : vec2f,
    @location(1) uv  : vec2f,
};

struct VertexOut {
    @builtin(position) clipPos : vec4f,
    @location(0) uv : vec2f,
};

struct Camera {
    viewProj : mat4x4f,
};

struct RasterObject {
    model : mat4x4f,

    uvOffset : vec2f,
    uvScale  : vec2f,

    opacity : f32,
    _pad0 : vec3f,
};

@group(0) @binding(0) var<uniform> uCamera : Camera;
@group(1) @binding(0) var<uniform> uObject : RasterObject;
@group(1) @binding(1) var uTexture : texture_2d<f32>;
@group(1) @binding(2) var uSampler : sampler;

@vertex
fn vs_main(in : VertexIn) -> VertexOut {
    var out : VertexOut;
    out.clipPos = uCamera.viewProj * uObject.model * vec4f(in.pos, 0.0, 1.0);
    
    out.uv = in.uv * uObject.uvScale + uObject.uvOffset;
    
    return out;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4f {
    return textureSample(uTexture, uSampler, in.uv);
}
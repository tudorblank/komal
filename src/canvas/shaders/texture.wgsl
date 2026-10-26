// Atlas-backed raster tiles, one instance per tile. Pixels are premultiplied.

struct InstanceIn {
    @location(0) center   : vec2f,   // world space
    @location(1) scale    : vec2f,   // world size (64 x 64)
    @location(2) uvOffset : vec2f,   // chunk origin in the atlas page, normalized
    @location(3) uvScale  : vec2f,   // chunk size in the atlas page, normalized
    @location(4) opacity  : f32,
};

struct VertexOut {
    @builtin(position) clipPos : vec4f,
    @location(0) uv            : vec2f,
    @location(1) opacity       : f32,
};

struct Camera {
    viewProj : mat4x4f,
};

@group(0) @binding(0) var<uniform> uCamera : Camera;
@group(1) @binding(0) var uTexture : texture_2d<f32>;
@group(1) @binding(1) var uSampler : sampler;

// Unit quad as a triangle strip; corner 0 is the chunk's top-left (y points down).
const kCorners = array<vec2f, 4>(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0), vec2f(1.0, 1.0));

@vertex
fn vs_main(@builtin(vertex_index) vi : u32, inst : InstanceIn) -> VertexOut {
    let corner = kCorners[vi];
    let worldPos = (corner - vec2f(0.5)) * inst.scale + inst.center;

    var out : VertexOut;
    out.clipPos = uCamera.viewProj * vec4f(worldPos, 0.0, 1.0);
    out.uv      = corner * inst.uvScale + inst.uvOffset;
    out.opacity = inst.opacity;
    return out;
}

@fragment
fn fs_main(in : VertexOut) -> @location(0) vec4f {
    return textureSample(uTexture, uSampler, in.uv) * in.opacity;   // premultiplied: scale everything
}

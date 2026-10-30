// A brush stroke on the GPU, for previews: its coverage over a raster's own pixels, the same
// maths as BrushStroke::composeTile on the CPU. Both textures are the size of the picture and
// premultiplied; pixels are read 1:1.
//
//   source = colour * coverage * opacity
//   paint:  source over the pixels          erase:  the pixels * (1 - source.a)
//   alpha locked: painting keeps each pixel's alpha (rgb rescaled to it), erasing does nothing

struct Params {
    color     : vec4f,   // premultiplied
    opacity   : f32,
    erase     : i32,
    keepAlpha : i32,
    pad       : i32,
};

@group(0) @binding(0) var<uniform> P : Params;
@group(1) @binding(0) var tPixels   : texture_2d<f32>;
@group(1) @binding(1) var tCoverage : texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {
    let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));   // one triangle covering the screen
    return vec4f(uv * 2.0 - 1.0, 0.0, 1.0);
}

@fragment
fn fs_main(@builtin(position) pos : vec4f) -> @location(0) vec4f {
    let p = vec2i(pos.xy);
    let below = textureLoad(tPixels, p, 0);
    let source = P.color * textureLoad(tCoverage, p, 0).a * P.opacity;
    if (P.erase != 0) {
        if (P.keepAlpha != 0) { return below; }
        return below * (1.0 - source.a);
    }
    let out = source + below * (1.0 - source.a);
    if (P.keepAlpha == 0) { return out; }
    if (below.a <= 0.0) { return vec4f(0.0); }
    return vec4f(out.rgb * (below.a / out.a), below.a);
}

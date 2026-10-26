// A view-sized texture drawn over the target pixel for pixel (premultiplied blending is in
// the pipeline).

@group(0) @binding(0) var tSource : texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {
    let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));   // one triangle covering the screen
    return vec4f(uv * 2.0 - 1.0, 0.0, 1.0);
}

@fragment
fn fs_main(@builtin(position) pos : vec4f) -> @location(0) vec4f {
    return textureLoad(tSource, vec2i(pos.xy), 0);
}

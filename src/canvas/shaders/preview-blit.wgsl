// A view-sized texture drawn over the target pixel for pixel (premultiplied blending is in
// the pipeline).
// Fragment stage only: the vertex stage is fullscreen.wgsl's (put before it).

@group(0) @binding(0) var tSource : texture_2d<f32>;

@fragment
fn fs_main(@builtin(position) pos : vec4f) -> @location(0) vec4f {
    return textureLoad(tSource, vec2i(pos.xy), 0);
}

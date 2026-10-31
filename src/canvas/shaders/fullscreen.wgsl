// The vertex stage of every full-screen pass: one triangle covering the target, from
// vertex_index (draw 3). Put before each full-screen shader (CanvasPipelines), which then holds
// its fragment stage only.

@vertex
fn vs_main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {
    let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    return vec4f(uv * 2.0 - 1.0, 0.0, 1.0);
}


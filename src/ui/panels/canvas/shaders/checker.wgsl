// The canvas background checkerboard, in screen pixels, anchored to the canvas origin.
// Fragment stage only: the vertex stage is fullscreen.wgsl's (put before it).

struct Checker {
    colorA : vec4f,   // the square at the origin
    colorB : vec4f,
    origin : vec2f,   // where the canvas origin is, in target pixels
    cell   : f32,     // square size, target pixels
    _pad   : f32,
};

@group(0) @binding(0) var<uniform> uChecker : Checker;

@fragment
fn fs_main(@builtin(position) pos : vec4f) -> @location(0) vec4f {
    let c = vec2i(floor((pos.xy - uChecker.origin) / uChecker.cell));
    return select(uChecker.colorA, uChecker.colorB, ((c.x + c.y) & 1) == 1);
}

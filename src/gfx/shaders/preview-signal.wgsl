// One node's signals on the GPU, for edit previews: the same maths as Node::fetch on the CPU.
// Every texture is the size of the view and premultiplied; pixels are read 1:1.
//
//   out = Main( Wet(wet) over Dry(dry) )
//   shaping with gains g (A, R, G, B):  rgb *= gA * gC,  a *= gA
//   a mask's factor: the mask over white, 1 - a + min(luma(rgb), a)

struct Params {
    mainK : vec4f, wetK : vec4f, dryK : vec4f,   // scales, A R G B
    mainM : vec4i, wetM : vec4i, dryM : vec4i,   // mask texture per channel, or -1
    flags : vec4i,                               // x: wet present, y: dry present
};

@group(0) @binding(0) var<uniform> P : Params;
@group(1) @binding(0)  var tWet : texture_2d<f32>;
@group(1) @binding(1)  var tDry : texture_2d<f32>;
@group(1) @binding(2)  var tM0  : texture_2d<f32>;
@group(1) @binding(3)  var tM1  : texture_2d<f32>;
@group(1) @binding(4)  var tM2  : texture_2d<f32>;
@group(1) @binding(5)  var tM3  : texture_2d<f32>;
@group(1) @binding(6)  var tM4  : texture_2d<f32>;
@group(1) @binding(7)  var tM5  : texture_2d<f32>;
@group(1) @binding(8)  var tM6  : texture_2d<f32>;
@group(1) @binding(9)  var tM7  : texture_2d<f32>;
@group(1) @binding(10) var tM8  : texture_2d<f32>;
@group(1) @binding(11) var tM9  : texture_2d<f32>;
@group(1) @binding(12) var tM10 : texture_2d<f32>;
@group(1) @binding(13) var tM11 : texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {
    let uv = vec2f(f32((i << 1u) & 2u), f32(i & 2u));   // one triangle covering the screen
    return vec4f(uv * 2.0 - 1.0, 0.0, 1.0);
}

fn maskPixel(i : i32, p : vec2i) -> vec4f {
    switch i {
        case 0:  { return textureLoad(tM0,  p, 0); }
        case 1:  { return textureLoad(tM1,  p, 0); }
        case 2:  { return textureLoad(tM2,  p, 0); }
        case 3:  { return textureLoad(tM3,  p, 0); }
        case 4:  { return textureLoad(tM4,  p, 0); }
        case 5:  { return textureLoad(tM5,  p, 0); }
        case 6:  { return textureLoad(tM6,  p, 0); }
        case 7:  { return textureLoad(tM7,  p, 0); }
        case 8:  { return textureLoad(tM8,  p, 0); }
        case 9:  { return textureLoad(tM9,  p, 0); }
        case 10: { return textureLoad(tM10, p, 0); }
        case 11: { return textureLoad(tM11, p, 0); }
        default: { return vec4f(0.0); }
    }
}

fn factor(i : i32, p : vec2i) -> f32 {
    if (i < 0) { return 1.0; }
    let m = maskPixel(i, p);
    let luma = dot(m.rgb, vec3f(54.0, 183.0, 19.0) / 256.0);
    return clamp(1.0 - m.a + min(luma, m.a), 0.0, 1.0);
}

fn shape(c : vec4f, k : vec4f, m : vec4i, p : vec2i) -> vec4f {
    let gA = k.x * factor(m.x, p);
    let g = vec3f(k.y * factor(m.y, p), k.z * factor(m.z, p), k.w * factor(m.w, p));
    return vec4f(c.rgb * gA * g, c.a * gA);
}

@fragment
fn fs_main(@builtin(position) pos : vec4f) -> @location(0) vec4f {
    let p = vec2i(pos.xy);
    var c = vec4f(0.0);
    if (P.flags.x != 0) { c = shape(textureLoad(tWet, p, 0), P.wetK, P.wetM, p); }
    if (P.flags.y != 0) {
        let d = shape(textureLoad(tDry, p, 0), P.dryK, P.dryM, p);
        c = c + d * (1.0 - c.a);   // wet over dry
    }
    return shape(c, P.mainK, P.mainM, p);
}

struct Params { radius: i32 };

@group(0) @binding(0) var inputTex: texture_2d<f32>;
@group(0) @binding(1) var outputTex: texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(2) var<uniform> params: Params;

@compute @workgroup_size(8, 8, 1)
fn cs_main(@builtin(global_invocation_id) gid: vec3<u32>) {
    let outSize = textureDimensions(outputTex);
    if (gid.x >= outSize.x || gid.y >= outSize.y) { return; }

    let R = params.radius;
    let sigma = f32(R) / 3.0;
    let cx = i32(gid.x) + R;
    let cy = i32(gid.y) + R;

    var colorSum = vec3<f32>(0.0);
    var alphaSum = 0.0;
    var totalWeight = 0.0;

    for (var dy = -R; dy <= R; dy = dy + 1) {
        for (var dx = -R; dx <= R; dx = dx + 1) {
            let s = textureLoad(inputTex, vec2<i32>(cx + dx, cy + dy), 0);
            let dist = length(vec2<f32>(f32(dx), f32(dy)));
            let weight = exp(-(dist * dist) / (2.0 * sigma * sigma));
            colorSum   = colorSum + s.rgb * s.a * weight;
            alphaSum   = alphaSum + s.a * weight;
            totalWeight = totalWeight + weight;
        }
    }

    let outAlpha = alphaSum / totalWeight;
    var outColor = vec3<f32>(0.0);
    if (outAlpha > 0.0001) { outColor = colorSum / alphaSum; }

    textureStore(outputTex, vec2<i32>(gid.xy), vec4<f32>(outColor, outAlpha));
}
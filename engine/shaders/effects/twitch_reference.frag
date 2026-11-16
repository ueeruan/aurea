#version 450
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D source;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D blurred;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region, sourceRegion, blurMap, lightColor, colorize, blurTint, blurParams;
    vec4 slideNow, slidePrevious, scale, radii, flags, holdout, splitNow, splitPrevious;
    vec4 blueNow, bluePrevious;
}
p;
vec2 border(vec2 uv) {
    if (p.flags.x < .5)
        return 1.0 - abs(mod(uv, 2.0) - 1.0);
    if (p.flags.x < 1.5)
        return fract(uv);
    return uv;
}
vec4 source_at(vec2 pixel) {
    return texture(source, border((pixel - p.sourceRegion.xy) / p.sourceRegion.zw));
}
vec4 lens(vec2 pixel) {
    vec4 sum = vec4(0);
    float weight = 0;
    for (int i = 0; i < 128; ++i) {
        float r = sqrt((float(i) + .5) / 128.0);
        float angle = float(i) * 2.399963229728653;
        sum += source_at(pixel + r * p.radii.xy * vec2(cos(angle), sin(angle)));
        weight += 1;
    }
    return sum / weight;
}
vec4 processed_at(vec2 pixel) {
    vec4 original = source_at(pixel);
    vec2 uv = border((pixel - p.sourceRegion.xy) / p.sourceRegion.zw);
    vec4 blur = p.radii.z > .5 ? lens(pixel) : texture(blurred, uv * p.blurMap.xy + p.blurMap.zw);
    float amount = p.blurParams.x;
    blur.rgb *= mix(vec3(1), p.blurTint.rgb, amount) * mix(1.0, p.blurParams.y, amount);
    vec3 a = original.a > 1e-6 ? original.rgb / original.a : vec3(0);
    vec3 b = blur.a > 1e-6 ? blur.rgb / blur.a : vec3(0);
    int mode = int(p.blurParams.w + .5);
    vec3 blend = b;
    if (mode == 2)
        blend = a + b - a * b;
    if (mode == 4)
        blend = a * b;
    if (mode == 5)
        blend = mix(2.0 * a * b, 1.0 - 2.0 * (1.0 - a) * (1.0 - b), step(vec3(.5), a));
    if (mode == 3)
        blend = a + b;
    float alpha = blur.a;
    vec4 result = mix(original, vec4(blend * alpha, alpha), mode == 0 ? amount : amount * p.blurParams.z);
    if (p.holdout.x > 0) {
        float luminance = dot(a, vec3(.2126, .7152, .0722));
        float sharpness = max(.001, p.holdout.y * .01);
        float preserve = smoothstep(p.holdout.x * .01 - sharpness, p.holdout.x * .01 + sharpness, luminance);
        result = mix(result, original, preserve);
    }
    vec3 rgb = result.a > 1e-6 ? result.rgb / result.a : vec3(0);
    if (p.lightColor.x < 1.0)
        rgb = (rgb - .5) / (p.lightColor.x * .8 + .2) + .5;
    rgb *= p.lightColor.x;
    if (p.lightColor.y > 0.0) {
        // Preserve the source's channel energy while moving it toward the selected tint.
        vec3 tinted = rgb * p.colorize.rgb;
        float remaining = dot(rgb - tinted, vec3(1));
        float equalShare = remaining / 3.0;
        float ceiling = min(1.0, max(tinted.r, max(tinted.g, tinted.b)) + equalShare);
        tinted = clamp(tinted, 0.0, 1.0);
        tinted.r += equalShare;
        remaining -= equalShare;
        remaining += max(0.0, tinted.r - ceiling);
        tinted.r = min(tinted.r, ceiling);
        tinted.g += remaining * .5;
        remaining *= .5;
        remaining += max(0.0, tinted.g - ceiling);
        tinted.g = min(tinted.g, ceiling);
        tinted.b = min(tinted.b + remaining, ceiling);
        rgb = mix(rgb, tinted, min(1.0, p.lightColor.y));
    }
    result.rgb = rgb * result.a;
    return result;
}
void main() {
    if (p.radii.w > .5) {
        o_color = lens(p.region.xy + v_uv * p.region.zw);
        return;
    }
    if (p.flags.y > .5) {
        o_color = vec4(0);
        return;
    }
    vec2 pixel = p.region.xy + v_uv * p.region.zw;
    float travel = length(p.slideNow.xy - p.slidePrevious.xy);
    travel = max(travel,length(p.splitNow.xy-p.splitPrevious.xy));
    travel = max(travel,length(p.splitNow.zw-p.splitPrevious.zw));
    travel = max(travel,length(p.blueNow.xy-p.bluePrevious.xy))*p.flags.z;
    float scaleTravel = abs(p.scale.x - p.scale.y) * length(pixel - p.scale.zw) * p.flags.w;
    int count = int(clamp(ceil(max(travel, scaleTravel)) + 1.0, 1.0, 128.0));
    vec4 sum = vec4(0);
    for (int i = 0; i < 128; ++i) {
        if (i >= count)
            break;
        float sampleTime = count == 1 ? 0.0 : float(i) / float(count - 1);
        vec2 translation = mix(p.slideNow.xy, p.slidePrevious.xy, sampleTime * p.flags.z);
        vec4 channels = mix(p.splitNow, p.splitPrevious, sampleTime * p.flags.z);
        vec2 blue = mix(p.blueNow.xy, p.bluePrevious.xy, sampleTime * p.flags.z);
        float scale = mix(p.scale.x, p.scale.y, sampleTime * p.flags.w);
        vec2 base = (pixel - p.scale.zw) * scale + p.scale.zw;
        vec2 at = base + translation;
        vec4 c = processed_at(at);
        if (p.holdout.z > 0.0) {
            vec4 r = processed_at(base + channels.xy);
            vec4 g = processed_at(base + channels.zw);
            vec4 b = processed_at(base + blue);
            c = vec4(r.r, g.g, b.b, (r.a + g.a + b.a) / 3.0);
        }
        sum += c;
    }
    o_color = sum / float(count);
}

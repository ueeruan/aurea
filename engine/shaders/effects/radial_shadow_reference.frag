#version 450
// Sombra radial (referência do AE). Comportamento descrito em
// RadialShadowReferenceEffect.cpp. Coordenadas em px da camada; o índice de
// pixel do efeito nativo é o centro do texel (sem meio pixel).
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_source;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_caster;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 light;        // L.xy, d, opacidade
    vec4 mode;         // vidro, influência, só a sombra
    vec4 color;
    vec4 outRegion;
    vec4 sourceRegion;
    vec4 shadowRegion;
    vec4 target;       // texels of the output
} p;

vec4 at_index(sampler2D image, vec4 region, vec2 index) {
    return texture(image, (index + 0.5 - region.xy) / region.zw);
}

vec4 border_texel(sampler2D image, ivec2 texel) {
    ivec2 size = textureSize(image, 0);
    if (any(lessThan(texel, ivec2(0))) || any(greaterThanEqual(texel, size))) return vec4(0.0);
    return texelFetch(image, texel, 0);
}

// Bilinear with exact weights (the native sampler rounds its result to 8 bits;
// hardware filtering quantises the fraction and moves edge alphas by one step).
vec4 bilinear_index(sampler2D image, vec4 region, vec2 index) {
    vec2 position = (index + 0.5 - region.xy) * vec2(textureSize(image, 0)) / region.zw - 0.5;
    ivec2 origin = ivec2(floor(position));
    vec2 w = position - vec2(origin);
    vec4 top = mix(border_texel(image, origin), border_texel(image, origin + ivec2(1, 0)), w.x);
    vec4 bottom = mix(border_texel(image, origin + ivec2(0, 1)), border_texel(image, origin + ivec2(1, 1)), w.x);
    return mix(top, bottom, w.y);
}

void main() {
    vec2 outSize = p.target.xy;
    vec2 pixel = floor(v_uv * outSize);
    vec2 index = p.outRegion.xy + (pixel + 0.5) * p.outRegion.zw / outSize - 0.5;
    vec2 source = (p.light.xy - index) * p.light.z + index;
    // The native kernel hands the sampler a 16.16 fixed point (truncated).
    vec2 magnitude = abs(source);
    source = sign(source) * (floor(magnitude) + trunc(fract(magnitude) * 65536.0) / 65536.0);
    vec4 projected = bilinear_index(u_caster, p.shadowRegion, source);
    float alpha8 = floor(clamp(projected.a, 0.0, 1.0) * 255.0 + 0.5);
    vec4 shadow = vec4(0.0);
    if (alpha8 > 0.0) {
        float alpha = floor(alpha8 * p.light.w) / 255.0;
        vec3 tint = p.color.rgb;
        if (p.mode.x > 0.5 && alpha8 < 255.0) {
            float t = (255.0 - alpha8) * p.mode.y / 127.5;
            vec3 own = projected.rgb / max(projected.a, 1e-6);
            if (t >= 1.0) tint = own;
            else if (t > 0.0) {
                float w = (1.0 - t) * (1.0 - t);
                tint = p.color.rgb * w + own * (1.0 - w);
            }
        }
        shadow = vec4(tint * alpha, alpha);
    }
    if (p.mode.z > 0.5) {
        o_color = shadow;
        return;
    }
    vec4 layer = at_index(u_source, p.sourceRegion, index);
    o_color = layer + shadow * (1.0 - layer.a);
}

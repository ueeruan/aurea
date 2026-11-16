#version 450
// =============================================================================
//  Aurea / shaders / effects / sharpen.frag
//
//  Nitidez por máscara de desfoque (unsharp mask) numa passada: o "desfoque" é
//  um núcleo gaussiano 3x3 (pesos 1-2-1), e a saída é
//
//      original + intensidade * (original - desfoque)
//
//  O raio do núcleo acompanha a escala de trabalho: no preview em 1/2, o passo
//  é meio texel da imagem reduzida — o mesmo detalhe da imagem cheia. Sem
//  isso, a nitidez no preview reduzido pareceria o dobro da do export.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;     // uv de entrada = v_uv * xy + zw
    vec4 texel;     // xy = passo do núcleo em uv, z = intensidade
} p;

vec4 straightSample(vec2 uv) {
    // The native 32-bpc convolution uses transparent black outside the image.
    // Explicit bounds keep this behavior with a clamp sampler on mobile GPUs.
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);
    vec4 value = texture(u_tex0, uv);
    value.rgb = value.a > 0.0 ? value.rgb / value.a : vec3(0.0);
    return value;
}

void main() {
    vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    vec2 d = p.texel.xy;
    vec4 c = texture(u_tex0, uv);
    if (p.texel.w > 0.5) {
        // Independent cross kernel from the measured PF convolve contract.
        // Fixed-point coefficients use a denominator of 2304.
        // The native 32-bpc path convolves straight linear RGB and alpha,
        // clamps the filtered alpha and negative color, then premultiplies.
        float centerWeight = floor((1.0 + p.texel.z * 0.0625) * 2304.0);
        float sideWeight = trunc((2304.0 - centerWeight) * 0.25);
        vec4 right = straightSample(uv + vec2(d.x, 0.0));
        vec4 left = straightSample(uv - vec2(d.x, 0.0));
        vec4 below = straightSample(uv + vec2(0.0, d.y));
        vec4 above = straightSample(uv - vec2(0.0, d.y));
        vec4 straightCenter = straightSample(uv);
        vec4 filtered = (straightCenter * centerWeight + (right + left + below + above) * sideWeight) / 2304.0;
        filtered.a = clamp(filtered.a, 0.0, 1.0);
        o_color = vec4(max(filtered.rgb, vec3(0.0)) * filtered.a, filtered.a);
        return;
    }
    vec4 blur = c * 4.0
        + (texture(u_tex0, uv + vec2(d.x, 0.0)) + texture(u_tex0, uv - vec2(d.x, 0.0))
         + texture(u_tex0, uv + vec2(0.0, d.y)) + texture(u_tex0, uv - vec2(0.0, d.y))) * 2.0
        + (texture(u_tex0, uv + d) + texture(u_tex0, uv - d)
         + texture(u_tex0, uv + vec2(d.x, -d.y)) + texture(u_tex0, uv + vec2(-d.x, d.y)));
    blur *= 1.0 / 16.0;
    vec4 o = c + p.texel.z * (c - blur);
    // Pré-multiplicado: a cor não pode passar do alfa nem ficar negativa.
    o.a = clamp(o.a, 0.0, 1.0);
    o.rgb = max(o.rgb, vec3(0.0));
    o_color = o;
}

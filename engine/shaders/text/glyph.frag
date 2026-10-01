#version 450
// =============================================================================
//  Aurea / shaders / text / glyph.frag
//
//  Preenchimento e contorno pela distância assinada do atlas: borda nítida em
//  qualquer escala (a largura da transição vem da derivada na tela) e
//  contorno de espessura uniforme sem rasterizar de novo. Saída
//  pré-multiplicada, linear.
// =============================================================================
#include "../common/bindings.glsl"

struct Glyph {
    vec4 rect;
    vec4 uv;
    vec4 fill;
    vec4 stroke;
    mat4 xform;
    vec4 misc;
    vec4 extra;
};

layout(set = 0, binding = AUREA_DATA, std430) readonly buffer Glyphs { Glyph g[]; } glyphs;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in int v_index;
layout(location = 0) out vec4 o_color;

const float kDistScale = 8.0;   // valor 0..255 por px da base (Text.hpp)

vec4 coverageAt(Glyph gl, vec2 uv, float aa) {
    if (any(lessThan(uv, gl.uv.xy)) || any(greaterThan(uv, gl.uv.zw))) return vec4(0.0);
    vec2 inset = 0.5 / vec2(textureSize(u_atlas, 0));
    float sd = (texture(u_atlas, clamp(uv, gl.uv.xy + inset, gl.uv.zw - inset)).r * 255.0 - 128.0) / kDistScale * gl.misc.z;
    float fillA = smoothstep(-aa, aa, sd) * gl.fill.a;
    float strokeA = gl.misc.w > 0.0 ? smoothstep(-aa, aa, sd + gl.misc.w) * gl.stroke.a : 0.0;
    return vec4(gl.fill.rgb * fillA + gl.stroke.rgb * strokeA * (1.0 - fillA), fillA + strokeA * (1.0 - fillA));
}

void main() {
    const Glyph gl = glyphs.g[v_index];
    if (gl.uv.x < 0.0) {
        // Fundo: caixa arredondada (raio em uv.y), borda suave de 1 px.
        const vec2 size = gl.rect.zw - gl.rect.xy;
        const float r = min(gl.uv.y, 0.5 * min(size.x, size.y));
        const vec2 q = abs(v_uv - 0.5 * size) - (0.5 * size - vec2(r));
        const float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
        const float aw = max(fwidth(d) * 0.5, 1e-4);
        const float a = (1.0 - smoothstep(-aw, aw, d)) * gl.fill.a;
        o_color = vec4(gl.fill.rgb * a, a);
        return;
    }
    // Blur coverage rather than widening the distance threshold: widening
    // left a visible rectangle wherever the finite atlas SDF saturated.
    if (gl.extra.x > 0.01) {
        vec2 uvPerPixel = (gl.uv.zw - gl.uv.xy) / max(gl.rect.zw - gl.rect.xy, vec2(0.001));
        float aa = max(0.25, length(fwidth(v_uv) / uvPerPixel) * 0.5);
        float sigma = gl.extra.x * 0.5;
        // Each kernel sample covers a cell of this size. Prefilter its SDF
        // coverage so thin strokes do not turn into repeated sharp copies.
        aa = max(aa, sigma);
        vec4 sum = vec4(0.0); float total = 0.0;
        for (int y = -3; y <= 3; ++y) for (int x = -3; x <= 3; ++x) {
            vec2 d = vec2(x, y);
            float weight = exp(-0.5 * dot(d, d));
            sum += coverageAt(gl, v_uv + d * sigma * uvPerPixel, aa) * weight;
            total += weight;
        }
        o_color = sum / total;
        return;
    }
    // Distância assinada em px da layer (positiva dentro do glifo).
    const float sd = (texture(u_atlas, v_uv).r * 255.0 - 128.0) / kDistScale * gl.misc.z;
    const float blur = gl.extra.x;
    const float w = max(fwidth(sd) * 0.5, 1e-4) + blur;
    const float fillA = smoothstep(-w, w, sd) * gl.fill.a;
    float strokeA = 0.0;
    if (gl.misc.w > 0.0) strokeA = smoothstep(-w, w, sd + gl.misc.w) * gl.stroke.a;
    // Preenchimento por cima do contorno.
    const vec3 rgb = gl.fill.rgb * fillA + gl.stroke.rgb * strokeA * (1.0 - fillA);
    const float a = fillA + strokeA * (1.0 - fillA);
    o_color = vec4(rgb, a);
}

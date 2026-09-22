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

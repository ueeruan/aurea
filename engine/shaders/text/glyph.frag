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
    vec4 xf;
    vec4 misc;
    vec4 pivot;
};

layout(set = 0, binding = AUREA_DATA, std430) readonly buffer Glyphs { Glyph g[]; } glyphs;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_atlas;

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in int v_index;
layout(location = 0) out vec4 o_color;

const float kDistScale = 8.0;   // valor 0..255 por px da base (Text.hpp)

void main() {
    const Glyph gl = glyphs.g[v_index];
    // Distância assinada em px da layer (positiva dentro do glifo).
    const float sd = (texture(u_atlas, v_uv).r * 255.0 - 128.0) / kDistScale * gl.misc.z;
    const float blur = gl.pivot.z;
    const float w = max(fwidth(sd) * 0.5, 1e-4) + blur;
    const float fillA = smoothstep(-w, w, sd) * gl.fill.a;
    float strokeA = 0.0;
    if (gl.misc.w > 0.0) strokeA = smoothstep(-w, w, sd + gl.misc.w) * gl.stroke.a;
    // Preenchimento por cima do contorno.
    const vec3 rgb = gl.fill.rgb * fillA + gl.stroke.rgb * strokeA * (1.0 - fillA);
    const float a = fillA + strokeA * (1.0 - fillA);
    o_color = vec4(rgb, a);
}

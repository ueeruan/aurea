#version 450
// =============================================================================
//  Aurea / shaders / mask / track_matte.frag
//
//  Track matte: a camada (já no espaço da composição) × o fator da matte no
//  mesmo pixel. Alfa = alfa da matte; luma = luminância BT.709 do valor
//  CODIFICADO da matte (é como o olho julga cinza médio) × alfa dela.
//  Invertidos: 1 − fator (onde a matte não existe, a camada aparece).
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // camada
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // matte

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 mode;   // x: 1 alfa, 2 alfa invertido, 3 luma, 4 luma invertido
} p;

void main() {
    vec4 c = texture(u_tex0, v_uv);
    vec4 m = texture(u_tex1, v_uv);
    int mode = int(p.mode.x + 0.5);
    float f = m.a;
    if (mode >= 3) {
        vec3 straight = m.a > 1e-5 ? m.rgb / m.a : vec3(0.0);
        vec3 enc = linear_to_srgb(straight);
        f = clamp(dot(enc, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0) * m.a;
    }
    if (mode == 2 || mode == 4) f = 1.0 - f;
    o_color = c * f;
}

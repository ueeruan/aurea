#version 450
// =============================================================================
//  Aurea / shaders / scene3d / ground_blur.frag
//
//  Desfoque gaussiano separável do chão (um eixo por passe):
//    modo 0 — o reflexo planar (RGBA pré-multiplicado, HDR codificado): o
//             desfoque pela rugosidade do chão;
//    modo 1 — o mapa de contato (profundidade vista de baixo, 0 = no chão,
//             1 = longe/nada): vira OCLUSÃO (1 − d)² no primeiro eixo e é
//             desfocado — a "pegada" macia da sombra de contato.
//  Taps espaçados (dir.xy = passo em uv): raio grande com 17 amostras
//  bilineares, custo fixo.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Blur {
    vec4 dir;   // xy = passo em uv entre taps
    vec4 cfg;   // x = modo, y = sigma (em passos)
} p;

vec4 fetch(vec2 uv) {
    vec4 s = textureLod(u_src, uv, 0.0);
    if (p.cfg.x > 0.5) {
        float o = 1.0 - clamp(s.r, 0.0, 1.0);
        o *= o;
        return vec4(o, o, o, o);
    }
    return s;
}

void main() {
    float sigma = max(p.cfg.y, 0.5);
    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    for (int i = -8; i <= 8; ++i) {
        float x = float(i) / sigma;
        float w = exp(-0.5 * x * x);
        sum += fetch(v_uv + p.dir.xy * float(i)) * w;
        wsum += w;
    }
    o_color = sum / wsum;
}

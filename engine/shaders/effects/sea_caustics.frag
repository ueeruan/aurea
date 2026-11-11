#version 450
// =============================================================================
//  Aurea / shaders / effects / sea_caustics.frag
//
//  Cáusticas (Luz, pacote "Mar"): a rede de luz que a superfície ondulada da
//  água foca no fundo, projetada na camada (o comportamento do "Caustics"
//  dos editores de movimento; desenho e contas próprios — sea_caustics() em
//  sea_common.glsl). A rede anda com o tempo, pode refratar a camada por
//  baixo e mistura por Adicionar, Tela, Sobrepor, Multiplicar ou sai sozinha.
//  Fica dentro do alfa da camada.
//
//  p0 = escala (px por célula), tempo × velocidade, intensidade, nitidez 0..1
//  p1 = mistura (0 adicionar, 1 tela, 2 sobrepor, 3 multiplicar, 4 só as cáusticas),
//       refração (px), ângulo (rad), semente
//  p2 = redes (1/2), —, —, —
//  color = cor da luz (linear)
// =============================================================================
#include "sea_common.glsl"

void main() {
    const vec2 px = sea_px();
    const float scale = max(p.p0.x, 1.0);
    const float tau = p.p0.y;
    const uint seed = uint(max(p.p1.w, 0.0));
    const vec2 local = mat2(cos(p.p1.z), -sin(p.p1.z), sin(p.p1.z), cos(p.p1.z)) * (px - p.box.xy) / scale;

    // Refração: a camada balança com o mesmo vaivém que move a rede.
    vec2 shift = vec2(0.0);
    if (p.p1.y > 0.0) {
        shift = vec2(sea_noise(local * 0.8 + vec2(tau * 0.25, 1.3), seed + 61u),
                     sea_noise(local * 0.8 + vec2(4.1, tau * 0.25), seed + 67u)) - 0.5;
        shift *= 2.0 * p.p1.y;
    }
    const vec4 col = sea_at(px + shift);
    if (col.a <= 1e-5) { o_color = vec4(0.0); return; }

    const float c = sea_caustics(local, tau, p.p0.w, seed, int(p.p2.x + 0.5));
    const vec3 light = p.color.rgb * c * p.p0.z;
    const int mode = int(p.p1.x + 0.5);
    const vec3 s = col.rgb / col.a;
    vec3 outRgb;
    if (mode == 4) {
        const float k = clamp(max(light.r, max(light.g, light.b)), 0.0, 1.0) * col.a;
        o_color = vec4(light * col.a, k);
        return;
    } else if (mode == 1) {
        const vec3 l = clamp(light, 0.0, 1.0);
        outRgb = s + l - s * l;
    } else if (mode == 2) {
        const vec3 top = clamp(0.5 + 0.5 * light, 0.0, 1.0);
        outRgb = mix(2.0 * s * top, 1.0 - 2.0 * (1.0 - s) * (1.0 - top), step(0.5, s));
    } else if (mode == 3) {
        const float shade = 1.0 - 0.6 * clamp(p.p0.z, 0.0, 1.0);
        outRgb = s * (shade + light);
    } else {
        outRgb = s + light;
    }
    o_color = vec4(max(outRgb, vec3(0.0)) * col.a, col.a);
}

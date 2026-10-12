#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_spotlight.frag
//
//  Holofote (o comportamento do CC Spotlight): uma luz a "Altura" acima da
//  camada, no ponto "Origem", aponta para o "Alvo"; o cone de "Abertura"
//  corta o plano numa elipse de luz com a borda suave. Fora dela fica o
//  "Ambiente".
//    0 luz sobre a camada — a camada iluminada (multiplicada pela luz);
//    1 só a luz          — a mancha de luz colorida, sem a camada.
//
//  p0 = origem (px), alvo (px); p1 = altura (px), meia abertura (rad),
//  suavidade (0..1), intensidade; p2 = ambiente, modo; color = cor (linear).
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const float H = max(p.p1.x, 1.0);
    const vec3 axis = normalize(vec3(p.p0.zw - p.p0.xy, H));
    const vec3 ray = normalize(vec3(px - p.p0.xy, H));
    const float phi = acos(clamp(dot(ray, axis), -1.0, 1.0));
    const float halfAngle = max(p.p1.y, 1e-3);
    const float soft = clamp(p.p1.z, 0.0, 1.0);
    const float spot = 1.0 - smoothstep(halfAngle * (1.0 - soft), halfAngle * (1.0 + 0.25 * soft) + 1e-4, phi);
    const vec3 light = p.color.rgb * (spot * max(p.p1.w, 0.0));
    if (int(p.p2.y + 0.5) == 1) {
        const float a = clamp(max(light.r, max(light.g, light.b)), 0.0, 1.0);
        o_color = vec4(min(light, vec3(a)), a);
        return;
    }
    const vec4 src = cc_at(px);
    o_color = vec4(src.rgb * (max(p.p2.x, 0.0) + light), src.a);
}

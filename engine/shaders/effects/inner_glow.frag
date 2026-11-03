#version 450
// =============================================================================
//  Aurea / shaders / effects / inner_glow.frag
//
//  Brilho interno (0.4.6): luz que entra pela BORDA da silhueta para dentro.
//  O quanto o pixel está perto da borda = 1 − média do alfa num disco em
//  volta dele (3 anéis × 16 amostras, raio em px da camada); a luz fica só
//  dentro do alfa da camada. Modo "centro": o inverso (acende o miolo).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;      // x = raio (px), y = intensidade, z = modo (0 borda, 1 centro), w = engasgo (0..1)
    vec4 p1;
    vec4 p2;
    vec4 p3;      // xy = tamanho da região da entrada (px)
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec4 src = texture(u_tex0, inUv);
    if (src.a <= 0.0) { o_color = src; return; }
    const vec2 px = 1.0 / max(p.p3.xy, vec2(1e-4));
    const float R = max(p.p0.x, 0.5);
    float sum = 0.0, wsum = 0.0;
    for (int ring = 1; ring <= 3; ++ring) {
        const float rr = R * float(ring) / 3.0;
        const float w = 1.0 / float(ring);
        for (int k = 0; k < 16; ++k) {
            const float a = (float(k) + 0.5 * float(ring)) * AUREA_TAU / 16.0;
            sum += texture(u_tex0, inUv + vec2(cos(a), sin(a)) * rr * px).a * w;
            wsum += w;
        }
    }
    float edge = 1.0 - sum / max(wsum, 1e-6);                 // 0 no miolo, sobe na borda
    if (p.p0.z > 0.5) edge = 1.0 - edge;
    edge = smoothstep(clamp(p.p0.w, 0.0, 0.95) * 0.5, 1.0, clamp(edge * 2.0, 0.0, 1.0));
    const float k = clamp(edge * p.p0.y, 0.0, 1.0) * p.color.a;
    // "Tela" da cor sobre a camada, só dentro dela: em pré-multiplicado,
    // rgb' = rgb + (a − rgb)·cor·k (o alfa não muda).
    o_color = vec4(src.rgb + (vec3(src.a) - src.rgb) * p.color.rgb * k, src.a);
}

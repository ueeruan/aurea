#version 450
// =============================================================================
//  Aurea / shaders / effects / optical_flares.frag
//
//  Optical Flares: um estilo = uma receita de elementos analíticos (custo fixo
//  por pixel, sem histórico): brilho central, risco anamórfico (+ risco
//  cruzado), raios em estrela, anel cromático e até 8 reflexos internos
//  (círculo ou hexágono) na reta luz → espelho pelo centro da camada.
//  Unidades: `unit` = menor lado da camada × tamanho.
// =============================================================================
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel;
    vec4 p0;          // luz x, y (px da camada), unit (px), brilho
    vec4 p1;          // centro x, y (px da camada), giro (rad), cintilação 0..1
    vec4 p2;          // região da saída (px da camada)
    vec4 p3;          // reflexos internos 0..∞, risco ×, raios ×, -
    vec4 color;       // tinta (linear), quanto tingir
    vec4 core;        // brilho largo, queda, miolo, tamanho do miolo
    vec4 coreColor;
    vec4 streak;      // intensidade, comprimento, espessura, risco cruzado
    vec4 streakColor;
    vec4 rays;        // intensidade, quantos, nitidez, queda
    vec4 rayColor;
    vec4 ring;        // intensidade, raio, largura, separação cromática
    vec4 ghostInfo;   // quantos, intensidade
    vec4 ghost[8];    // t na reta, tamanho, forma (0 círculo, 1 hexágono), suavidade
    vec4 ghostColor[8];
} p;

float hexagon(vec2 q, float r) {
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    q = abs(q);
    q -= 2.0 * min(dot(k.xy, q), 0.0) * k.xy;
    q -= vec2(clamp(q.x, -k.z * r, k.z * r), r);
    return length(q) * sign(q.y);
}

void main() {
    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const vec2 light = p.p0.xy;
    const float unit = max(p.p0.z, 1.0);
    const vec2 d = (point - light) / unit;
    const float r = length(d);
    const float c = cos(p.p1.z), s = sin(p.p1.z);
    const vec2 q = vec2(c * d.x + s * d.y, -s * d.x + c * d.y);

    // Brilho central: halo largo exponencial + miolo quente 1/r².
    vec3 flare = p.coreColor.rgb * (p.core.x * exp(-r * p.core.y)
        + p.core.z * p.core.w * p.core.w / (r * r + p.core.w * p.core.w));
    // Risco anamórfico (horizontal no giro) e o cruzado (vertical, mais fraco).
    const float thick = max(p.streak.z, 1e-4), len = max(p.streak.y, 1e-4);
    float streak = exp(-abs(q.y) / thick) * exp(-abs(q.x) / len);
    streak += p.streak.w * exp(-abs(q.x) / thick) * exp(-abs(q.y) / (len * 0.35));
    flare += p.streakColor.rgb * (p.streak.x * p.p3.y * streak);
    // Raios em estrela.
    if (p.rays.x > 0.0 && p.rays.y >= 2.0) {
        const float a = atan(q.y, q.x);
        const float spokes = pow(abs(cos(a * p.rays.y * 0.5)), max(p.rays.z, 1.0));
        flare += p.rayColor.rgb * (p.rays.x * p.p3.z * spokes * exp(-r * p.rays.w));
    }
    // Anel cromático (cada canal num raio um pouco diferente).
    if (p.ring.x > 0.0) {
        const vec3 radius = p.ring.y + vec3(p.ring.w, 0.0, -p.ring.w);
        const vec3 band = (vec3(r) - radius) / max(p.ring.z, 1e-4);
        flare += p.ring.x * exp(-band * band);
    }
    // Reflexos internos na reta luz → espelho (pelo centro da camada).
    const vec2 mirror = 2.0 * p.p1.xy - light;
    const int count = int(p.ghostInfo.x + 0.5);
    for (int i = 0; i < 8; ++i) {
        if (i >= count) break;
        const vec4 g = p.ghost[i];
        const vec2 at = mix(light, mirror, g.x);
        const vec2 gd = (point - at) / unit;
        const float size = max(g.y, 1e-4);
        const float sdf = g.z > 0.5 ? hexagon(gd, size) : length(gd) - size;
        const float soft = max(g.w * size, 1e-4);
        const float body = 1.0 - smoothstep(-soft, soft * 0.25, sdf);
        // Borda um pouco mais clara (o anel da íris de vidro).
        const float rim = exp(-pow(sdf / max(soft * 0.6, 1e-4), 2.0)) * 0.6;
        flare += p.ghostColor[i].rgb * (body * 0.6 + rim) * p.ghostInfo.y * p.p3.x;
    }
    flare *= p.p0.w * p.p1.w;
    flare = mix(flare, flare * p.color.rgb, clamp(p.color.w, 0.0, 1.0));

    vec4 base = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    const float coverage = clamp(max(flare.r, max(flare.g, flare.b)), 0.0, 1.0);
    o_color = vec4(base.rgb + flare, base.a + coverage * (1.0 - base.a));
}

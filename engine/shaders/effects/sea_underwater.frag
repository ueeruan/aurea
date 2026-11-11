#version 450
// =============================================================================
//  Aurea / shaders / effects / sea_underwater.frag
//
//  Embaixo d'água (pacote "Mar"): a camada vista de dentro da água, num passe.
//    1. ondulação: a imagem balança com senos cruzados + ruído largo;
//    2. cáusticas: a rede de luz focada pelas ondas, mais forte perto da
//       superfície (topo da camada);
//    3. profundidade: o vermelho some primeiro (absorção) e a água fica mais
//       densa e escura para baixo (névoa azul);
//    4. raios de luz descendo de cima, inclinados, que se mexem devagar;
//    5. partículas em suspensão subindo (plâncton, poeira).
//  Tudo multiplica o alfa da camada (texto continua texto). Função pura do
//  ponto e do tempo: o mesmo quadro sai igual no preview e no export.
//
//  p0 = ondulação (px), tamanho da onda (px), tempo × velocidade, cáusticas 0..4
//  p1 = escala das cáusticas (px), profundidade 0..1, raios 0..1, partículas 0..1
//  p2 = tamanho da partícula (px), semente, redes de cáusticas (1/2), camadas de partículas (1/2)
//  color = cor da água (linear)
// =============================================================================
#include "sea_common.glsl"

vec3 particles(vec2 px, float tau, float size, uint seed, float density, float layerScale, uint salt) {
    const float cell = max(size * 16.0 * layerScale, 2.0);
    // Sobem devagar (o campo desce) e balançam de lado.
    vec2 q = px / cell + vec2(0.0, tau * 0.22 / layerScale);
    const vec2 c = floor(q);
    const float h = aurea_hash3(c, seed + salt);
    if (h > density * 0.7) return vec3(0.0);
    const float hx = aurea_hash3(c, seed + salt + 1u), hy = aurea_hash3(c, seed + salt + 2u);
    const vec2 center = c + vec2(0.25 + 0.5 * hx + 0.12 * sin(tau * 0.8 + AUREA_TAU * h), 0.25 + 0.5 * hy);
    const float r = size * layerScale * (0.6 + 0.8 * hy);
    const float d = length((q - center) * cell);
    const float dotv = 1.0 - smoothstep(r * 0.25, r, d);
    return vec3(0.82, 0.95, 1.0) * dotv * (0.35 + 0.65 * hx);
}

void main() {
    const vec2 px = sea_px();
    const float tau = p.p0.z;
    const uint seed = uint(max(p.p2.y, 0.0));
    // 0 no topo da camada (a superfície), 1 no pé (o fundo). box = x, y, largura, altura.
    const float depthN = clamp((px.y - p.box.y) / max(p.box.w, 1.0), 0.0, 1.0);

    // 1. Ondulação.
    vec2 shift = vec2(0.0);
    if (p.p0.x > 0.0) {
        const vec2 q = px / max(p.p0.y, 1.0);
        shift = vec2(sin(q.y * 2.1 + tau * 1.3 + 1.7 * sin(q.x * 0.9 + tau * 0.7)),
                     cos(q.x * 1.7 - tau * 1.1 + 1.3 * sin(q.y * 1.1 - tau * 0.5)));
        shift += 0.6 * (vec2(sea_noise(q * 0.7 + vec2(tau * 0.2, 0.0), seed + 3u),
                             sea_noise(q * 0.7 + vec2(0.0, tau * 0.2), seed + 5u)) - 0.5);
        shift *= p.p0.x * 0.7;
    }
    vec4 col = sea_at(px + shift);
    if (col.a <= 1e-5) { o_color = vec4(0.0); return; }

    // 2. Cáusticas (mais fortes perto da superfície).
    if (p.p0.w > 0.0) {
        const float c = sea_caustics((px + shift * 0.5) / max(p.p1.x, 1.0), tau * 0.8, 0.55, seed + 17u, int(p.p2.z + 0.5));
        const float light = c * p.p0.w * (1.0 - 0.55 * depthN);
        col.rgb += (col.rgb * 1.1 + vec3(0.07, 0.09, 0.09) * col.a) * light;
    }

    // 3. Profundidade: absorção do vermelho e névoa da cor da água.
    const float depth = p.p1.y;
    if (depth > 0.0) {
        const vec3 water = p.color.rgb;
        const vec3 absorb = mix(vec3(1.0), clamp(water * 2.4 + vec3(0.05), vec3(0.0), vec3(1.15)), depth * 0.85);
        col.rgb *= absorb;
        const float fog = depth * (0.25 + 0.6 * depthN);
        col.rgb = mix(col.rgb, water * (1.15 - 0.65 * depthN) * col.a, clamp(fog, 0.0, 0.9));
    }

    // 4. Raios de luz de cima (inclinados, mexendo devagar).
    if (p.p1.z > 0.0) {
        const float xr = (px.x - p.box.x) / max(p.box.z, 1.0) + depthN * 0.22;
        const float r1 = sea_noise(vec2(xr * 9.0, tau * 0.12), seed + 23u);
        const float r2 = sea_noise(vec2(xr * 23.0 + 3.0, tau * 0.21), seed + 29u);
        const float shaft = smoothstep(0.35, 1.0, r1 * 0.65 + r2 * 0.45);
        const float fall = pow(1.0 - depthN, 1.6);
        col.rgb += vec3(0.72, 0.93, 1.0) * shaft * fall * p.p1.z * 0.75 * col.a;
    }

    // 5. Partículas em suspensão.
    if (p.p1.w > 0.0) {
        vec3 dots = particles(px, tau, max(p.p2.x, 0.3), seed, p.p1.w, 1.0, 101u);
        if (p.p2.w > 1.5) dots += 0.6 * particles(px + vec2(37.0, 11.0), tau * 0.7, max(p.p2.x, 0.3), seed, p.p1.w, 0.6, 211u);
        col.rgb += dots * col.a * (0.4 + 0.6 * p.p1.w);
    }

    o_color = vec4(max(col.rgb, vec3(0.0)), col.a);
}

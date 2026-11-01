#version 450
// =============================================================================
//  Aurea / shaders / effects / disintegrate.vert
//
//  Desintegrar: a camada é uma grade de fragmentos quadrados (6 vértices por
//  fragmento, sem vertex buffer). Cada fragmento se solta num instante próprio
//  — a frente varre a camada na direção escolhida, misturada a um sorteio pela
//  aleatoriedade — e depois vive uma fração fixa da conclusão: voa na direção
//  (± espalhamento) com a velocidade, segue um campo de fluxo suave
//  (turbulência), cai (gravidade), gira, encolhe e esmaece. Fragmento preso ou
//  já apagado não desenha: o que ainda está preso sai no passe de composição
//  (disintegrate_compose.frag), com a MESMA conta de instante (release_at).
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 src;      // região da camada (px): x, y, largura, altura
    vec4 dst;      // região da saída (px)
    vec4 grid;     // colunas, linhas, lado do fragmento (px), semente
    vec4 front;    // direção (x, y), projeção mínima, 1 / extensão da varredura
    vec4 timing;   // conclusão (0..1), aleatoriedade (0..1), vida (fração), esmaecer (0..1)
    vec4 motion;   // velocidade (px), espalhamento (rad), turbulência (px), gravidade (px)
    vec4 glow;     // cor do brilho (linear), intensidade
    vec4 texel;    // px por texel, -, -, -
    vec4 uvMap;    // (composição)
    vec4 mode;     // x, y (composição); z = 1 poeira; w = escala do ruído da poeira (px)
} p;

layout(location = 0) out vec2 v_uv;             // uv da camada
layout(location = 1) out vec2 v_local;          // |v_local| = 1 na borda do quadrado
layout(location = 2) flat out vec4 v_info;      // opacidade, brilho, meio lado em texels, -

float hash1(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return float(x & 0xFFFFFFu) / 16777216.0;
}

uint cell_id(ivec2 c) {
    return (uint(c.x) * 73856093u) ^ (uint(c.y) * 19349663u) ^ (uint(p.grid.w) * 83492791u + 0x9E3779B9u);
}

// Poeira (mode.z = 1): ruído de valor suave — a frente fica orgânica (manchas,
// não células soltas). Mesma conta no vértice e na composição.
float dust_noise(vec2 q) {
    const vec2 i = floor(q), f = fract(q);
    const vec2 u = f * f * (3.0 - 2.0 * f);
    const uint s = uint(p.grid.w) * 2654435761u;
    const float a = hash1((uint(int(i.x)) * 73856093u) ^ (uint(int(i.y)) * 19349663u) ^ s);
    const float b = hash1((uint(int(i.x) + 1) * 73856093u) ^ (uint(int(i.y)) * 19349663u) ^ s);
    const float c = hash1((uint(int(i.x)) * 73856093u) ^ (uint(int(i.y) + 1) * 19349663u) ^ s);
    const float d = hash1((uint(int(i.x) + 1) * 73856093u) ^ (uint(int(i.y) + 1) * 19349663u) ^ s);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Instante (0..1 da conclusão) em que o fragmento da célula c se solta.
float release_at(ivec2 c) {
    const vec2 center = p.src.xy + (vec2(c) + 0.5) * p.grid.z;
    const float s = clamp((dot(center, p.front.xy) - p.front.z) * p.front.w, 0.0, 1.0);
    float h = hash1(cell_id(c));
    if (p.mode.z > 0.5) {
        // Poeira: duas oitavas de ruído pela escala + um grão por célula.
        const vec2 q = center / max(p.mode.w, 1.0);
        h = 0.62 * dust_noise(q) + 0.26 * dust_noise(q * 2.7 + 17.0) + 0.12 * h;
        // O ruído somado se junta perto de 0,5: esticar devolve uma frente que
        // anda por igual (na metade da conclusão sobra parte da camada).
        h = clamp((h - 0.5) * 2.4 + 0.5, 0.0, 1.0);
    }
    return mix(s, h, p.timing.y) * (1.0 - p.timing.z);
}

void main() {
    const vec2 corners[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    const uint vid = uint(gl_VertexIndex);
    const uint idx = vid / 6u;
    const vec2 q = corners[vid % 6u];
    const uint cols = max(uint(p.grid.x + 0.5), 1u);
    const ivec2 c = ivec2(int(idx % cols), int(idx / cols));
    v_uv = vec2(0.0);
    v_local = q;
    v_info = vec4(0.0);

    const float age = (p.timing.x - release_at(c)) / max(p.timing.z, 1e-4);
    if (age <= 0.0 || age >= 1.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   // preso ou já apagado
        return;
    }
    const float side = p.grid.z;
    const vec2 center = p.src.xy + (vec2(c) + 0.5) * side;

    const uint id = cell_id(c) * 7u + 5u;
    const float h1 = hash1(id * 747796405u + 1u);
    const float h2 = hash1(id * 2891336453u + 2u);
    const float h3 = hash1(id * 1597334677u + 3u);
    const float h4 = hash1(id * 3266489917u + 4u);

    const bool dust = p.mode.z > 0.5;
    // Voo: começa devagar (o fragmento descola) e acelera. A poeira é levada
    // pelo vento: arranca mais devagar e acelera mais (age^1,6).
    const float ang = atan(p.front.y, p.front.x) + (h1 * 2.0 - 1.0) * p.motion.y;
    const vec2 dir = vec2(cos(ang), sin(ang));
    const float travel = dust ? p.motion.x * (0.6 + 0.8 * h2) * pow(age, 1.6)
                              : p.motion.x * (0.55 + 0.9 * h2) * age * (0.35 + 0.65 * age);
    vec2 pos = center + dir * travel;
    pos.y += p.motion.w * age * age;
    // Fluxo: dois senos cruzados, fase por fragmento, amplitude cresce com a idade.
    // Na poeira o redemoinho é mais fino (escala 45 px) e cresce mais rápido.
    const vec2 w = pos / (dust ? 45.0 : 90.0);
    const float ph = h3 * 6.28318530718;
    const vec2 flow = vec2(sin(w.y * 1.7 + age * 3.1 + ph) + 0.5 * sin(w.x * 2.9 - age * 2.3),
                           cos(w.x * 1.3 - age * 2.7 + ph) + 0.5 * cos(w.y * 2.3 + age * 1.9));
    pos += flow * p.motion.z * (dust ? sqrt(age) * 0.8 : age * 0.6);

    // A poeira não gira (é um grão redondo) e some encolhendo até nada.
    const float spin = dust ? 0.0 : (h4 * 2.0 - 1.0) * 6.28318530718 * 1.25 * age;
    const float scale = dust ? mix(1.0, 0.05, age) * (0.7 + 0.6 * h4) : mix(1.0, 0.3, age);
    const float halfSide = 0.5 * side * scale;
    const float margin = max(p.texel.x, 1e-3);   // um texel, em px
    const float grow = (halfSide + margin) / max(halfSide, 1e-4);
    const vec2 lq = q * (halfSide + margin);
    const float cs = cos(spin), sn = sin(spin);
    const vec2 at = pos + vec2(cs * lq.x - sn * lq.y, sn * lq.x + cs * lq.y);
    const vec2 ndc = (at - p.dst.xy) / max(p.dst.zw, vec2(1e-4)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.5, 1.0);

    v_uv = (center + q * 0.5 * side * grow - p.src.xy) / max(p.src.zw, vec2(1e-4));
    v_local = q * grow;
    const float alpha = clamp((1.0 - age) / max(p.timing.w, 1e-3), 0.0, 1.0);
    const float hot = p.glow.w * (1.0 - age) * (1.0 - age);
    v_info = vec4(alpha, hot, halfSide / margin, 0.0);
}

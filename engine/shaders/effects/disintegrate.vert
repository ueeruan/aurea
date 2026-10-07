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
    vec4 mode;     // (composição)
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

// Instante (0..1 da conclusão) em que o fragmento da célula c se solta.
float release_at(ivec2 c) {
    const vec2 center = p.src.xy + (vec2(c) + 0.5) * p.grid.z;
    const float s = clamp((dot(center, p.front.xy) - p.front.z) * p.front.w, 0.0, 1.0);
    const float h = hash1(cell_id(c));
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

    // Voo: começa devagar (o fragmento descola) e acelera.
    const float ang = atan(p.front.y, p.front.x) + (h1 * 2.0 - 1.0) * p.motion.y;
    const vec2 dir = vec2(cos(ang), sin(ang));
    const float travel = p.motion.x * (0.55 + 0.9 * h2) * age * (0.35 + 0.65 * age);
    vec2 pos = center + dir * travel;
    pos.y += p.motion.w * age * age;
    // Fluxo: dois senos cruzados, fase por fragmento, amplitude cresce com a idade.
    const vec2 w = pos / 90.0;
    const float ph = h3 * 6.28318530718;
    const vec2 flow = vec2(sin(w.y * 1.7 + age * 3.1 + ph) + 0.5 * sin(w.x * 2.9 - age * 2.3),
                           cos(w.x * 1.3 - age * 2.7 + ph) + 0.5 * cos(w.y * 2.3 + age * 1.9));
    pos += flow * p.motion.z * age * 0.6;

    const float spin = (h4 * 2.0 - 1.0) * 6.28318530718 * 1.25 * age;
    const float scale = mix(1.0, 0.3, age);
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

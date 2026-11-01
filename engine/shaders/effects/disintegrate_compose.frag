#version 450
// =============================================================================
//  Aurea / shaders / effects / disintegrate_compose.frag
//
//  Fecha o Desintegrar e as Bolas: os fragmentos/bolas (tex0, na região da
//  saída) "sobre" o que ainda está preso da camada e, por fim, a mistura com a
//  camada original.
//    mode.y = 1 (Desintegrar): o resto é a camada nas células cujo instante de
//             soltura (release_at, a mesma conta do disintegrate.vert) ainda
//             não chegou; perto da frente ganha o brilho da borda.
//    mode.y = 0 (Bolas): não há resto, só as bolas.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // fragmentos / bolas
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a camada

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 src;      // região da camada (px)
    vec4 dst;      // região da saída (px)
    vec4 grid;     // colunas, linhas, lado do fragmento (px), semente
    vec4 front;    // direção (x, y), projeção mínima, 1 / extensão
    vec4 timing;   // conclusão, aleatoriedade, vida, esmaecer
    vec4 motion;
    vec4 glow;     // cor do brilho (linear), intensidade
    vec4 texel;
    vec4 uvMap;    // uv da saída -> uv da camada
    vec4 mode;     // x = mistura, y = desintegrar, z = poeira, w = escala do ruído da poeira (px)
} p;

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
    const vec4 src = texture(u_tex1, v_uv * p.uvMap.xy + p.uvMap.zw);
    const vec4 parts = clamp(texture(u_tex0, v_uv), 0.0, 1.0);
    vec4 rest = vec4(0.0);
    if (p.mode.y > 0.5) {
        const vec2 px = p.dst.xy + v_uv * p.dst.zw;
        const vec2 cf = floor((px - p.src.xy) / max(p.grid.z, 1e-4));
        const float cols = max(p.grid.x, 1.0), rows = max(p.grid.y, 1.0);
        if (cf.x >= 0.0 && cf.y >= 0.0 && cf.x < cols && cf.y < rows) {
            const float left = release_at(ivec2(cf)) - p.timing.x;   // > 0: ainda presa
            if (left > 0.0) {
                rest = src;
                const float hot = p.glow.w * (1.0 - smoothstep(0.0, 0.05, left));
                rest.rgb += p.glow.rgb * hot * rest.a;
            }
        }
    }
    const vec4 fx = parts + rest * (1.0 - parts.a);
    o_color = mix(src, fx, clamp(p.mode.x, 0.0, 1.0));
}

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
    vec4 mode;     // x = mistura, y = desintegrar
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

float release_at(ivec2 c) {
    const vec2 center = p.src.xy + (vec2(c) + 0.5) * p.grid.z;
    const float s = clamp((dot(center, p.front.xy) - p.front.z) * p.front.w, 0.0, 1.0);
    const float h = hash1(cell_id(c));
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

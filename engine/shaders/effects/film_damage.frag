#version 450
// =============================================================================
//  Aurea / shaders / effects / film_damage.frag  (Fase 7.3 §31)
//
//  Filme danificado. Seis defeitos, cada um com o seu controle, porque "dano
//  de filme" é uma família e ninguém quer os seis sempre:
//
//    1. POEIRA: pontos claros e escuros, do tamanho de um grão de sujeira
//    2. RISCOS: linhas verticais que aparecem e somem
//    3. PISCAR: a exposição oscila quadro a quadro
//    4. BALANÇO DE PORTA: a imagem inteira treme devagar (a película não
//       assenta igual no quadro)
//    5. QUEIMADO: clareia as bordas como se a luz tivesse entrado
//    6. EMENDA: a linha de colagem da fita, que passa pelo quadro
//
//  Todos vêm de uma hash do (quadro, semente), então o defeito é diferente a
//  cada quadro — e idêntico entre o preview e o export.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = poeira, y = riscos, z = piscar, w = balanço de porta (px)
    vec4 p1;   // x = queimado, y = emenda (0/1), z = semente, w = mistura
    vec4 p2;   // x = tamanho da poeira (px), y = comprimento do risco (0..1), z = velocidade da emenda, w = cor do queimado aceso
    vec4 p3;   // x = quadro local
    vec4 color;
} p;

float film_random(uint index, uint seed) {
    return aurea_hash(uvec2(index, seed));
}

float film_flicker(float time, uint seed) {
    // A continuous field gives repeatable exposure even when scrubbing
    // between frames. Phase and salt align the two measured exposure samples
    // while every time and user seed still follow the same noise function.
    float tick = time * p.p2.y + 3.40625;
    uint cell = uint(max(0.0, floor(tick)));
    float frac = fract(tick);
    const uint field = seed ^ 4u;
    float noise = mix(film_random(cell, field), film_random(cell + 1u, field),
                      frac * frac * (3.0 - 2.0 * frac));
    return max(0.0, 1.0 + p.p2.x * (noise * 2.0 - 1.0));
}

float film_specks(vec2 uv, float count, float divisions, float radius, uint seed) {
    vec2 q = uv * divisions;
    uvec2 cell = uvec2(ivec2(floor(q)));
    uint at = cell.x + uint(divisions) * cell.y;
    if (film_random(at, seed) >= count / (divisions * divisions)) return 0.0;
    vec2 position = 0.15 + 0.7 * vec2(film_random(at + 71u, seed), film_random(at + 257u, seed));
    vec2 delta = (fract(q) - position) / divisions / max(p.uvMap.zw, vec2(1e-7));
    float edge = length(delta) - radius * (0.65 + 0.7 * film_random(at + 937u, seed));
    return 1.0 - smoothstep(-0.45, 0.55, edge);
}

float film_scratches(vec2 uv, uint seed) {
    float result = 0.0;
    for (int i = 0; i < int(p.p0.z); ++i) {
        uint strand = uint(i) * 181u;
        float x = film_random(strand + 1u, seed);
        float slope = (film_random(strand + 15u, seed) - 0.5) * 0.001;
        float reach = 0.44 + 0.55 * film_random(strand + 79u, seed);
        float center = film_random(strand + 109u, seed);
        float yGate = 1.0 - smoothstep(reach * 0.46, reach * 0.5, abs(uv.y - center));
        float gap = smoothstep(0.22, 0.39, aurea_value_noise(vec2(uv.y * 120.0, float(i) + float(seed & 255u))));
        float distancePx = abs(uv.x - x - slope * (uv.y - center)) / max(p.uvMap.z, 1e-7);
        float width = max(0.0001, p.texel.w * (0.6 + 0.8 * film_random(strand + 197u, seed)));
        float line = (1.0 - smoothstep(0.0, 0.65 + width, distancePx)) * gap * yGate;
        float tint = film_random(strand + 31u, seed) < 0.1 ? -0.45 : 1.0;
        result += line * tint;
    }
    return clamp(result, -1.0, 1.0);
}

float film_hairs(vec2 uv, uint seed) {
    float result = 0.0;
    for (int i = 0; i < int(p.color.y); ++i) {
        uint strand = uint(i) * 131u;
        vec2 start = vec2(film_random(strand + 17u, seed), film_random(strand + 51u, seed));
        float angle = film_random(strand + 83u, seed) * AUREA_TAU;
        vec2 direction = vec2(cos(angle), sin(angle));
        vec2 delta = (uv - start) * vec2(1.0, p.p3.y);
        float along = dot(delta, direction);
        float lengthNorm = 0.012 + 0.028 * film_random(strand + 209u, seed);
        float t = along / lengthNorm;
        float bend = sin(t * 3.14159265) * 0.006;
        float across = abs(dot(delta, vec2(-direction.y, direction.x)) - bend);
        float shape = smoothstep(0.0, 0.12, t) * (1.0 - smoothstep(0.86, 1.0, t));
        result = max(result, shape * (1.0 - smoothstep(0.0, 0.85 * p.uvMap.z, across)));
    }
    return result;
}

vec4 photochemical_film() {
    uint seed = uint(p.p3.z);
    uint frame = uint(max(p.p3.w, 0.0));
    float phase = p.p3.x * p.texel.z * AUREA_TAU;
    vec2 weave = vec2(sin(phase + float(seed & 7u)) * 0.60,
                      sin(phase * 0.73 + float(seed & 15u)) * 0.40) * p.p0.w;
    vec4 source = unpremultiply(texture(u_tex0, v_uv + weave * p.uvMap.zw));
    vec3 encoded = aurea_linear_to_srgb(max(source.rgb, vec3(0.0)));
    float luma = clamp(dot(encoded, vec3(0.3125, 0.5, 0.1875)), 0.0, 1.0);
    encoded = mix(vec3(luma), encoded, p.p2.z) * p.p2.w + p.color.z;

    vec2 ellipse = 2.0 * (v_uv - vec2(0.5)) * vec2(1.0, p.p3.y / p.p1.w);
    float distance = length(ellipse);
    float fade = p.p1.z > 1e-5 ? clamp((distance - p.p1.y) / p.p1.z + 0.5, 0.0, 1.0)
                                : step(p.p1.y, distance);
    fade = mix(fade, 0.5 - 0.5 * cos(fade * 3.14159265), 0.4);
    encoded *= film_flicker(p.p3.x, seed) * (1.0 - p.p1.x * fade);

    if (p.p0.x > 0.0) {
        uvec2 pixel = uvec2(ivec2(floor(v_uv / max(p.uvMap.zw, vec2(1e-7)))));
        uvec2 cell = pixel ^ uvec2(frame * 2654435761u, seed);
        const uvec2 rSalt = uvec2(17u, 409u), gSalt = uvec2(141u, 178u), bSalt = uvec2(211u, 537u);
        vec3 first = vec3(aurea_hash(cell ^ rSalt), aurea_hash(cell ^ gSalt), aurea_hash(cell ^ bSalt));
        cell ^= uvec2(0x95a4a359u, 0x0babe065u);
        vec3 second = vec3(aurea_hash(cell ^ rSalt), aurea_hash(cell ^ gSalt), aurea_hash(cell ^ bSalt));
        float weight = sin(luma * 3.14159265) + mix(p.uvMap.x, p.uvMap.y, luma);
        encoded += (first + second - 1.0) * vec3(0.90, 1.0, 1.6) * (0.78 * weight * p.p0.x);
    }

    float dust = film_specks(v_uv, p.p0.y, 16.0, 0.8, seed ^ (frame * 0x9e3779b9u));
    float stains = film_specks(v_uv, p.color.x, 4.0, 3.1, seed ^ ((frame / 4u) * 0xa6c37d8bu));
    float scratches = film_scratches(v_uv, seed ^ ((frame / 5u) * 0x25673d8bu));
    float hairs = film_hairs(v_uv, seed ^ ((frame / 3u) * 0x906351a3u));
    float mask = max(max(dust * 0.75, stains * 0.50), max(scratches * 0.82, hairs * 0.82));
    encoded *= 1.0 - mask;
    encoded += max(-scratches, 0.0) * 0.1;
    return premultiply(vec4(aurea_srgb_to_linear(max(encoded, vec3(0.0))), source.a));
}

void main() {
    if (p.color.w > 0.5) {
        o_color = photochemical_film();
        return;
    }
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const float frame = p.p3.x;

    // 4. Balanço de porta: dois senos lentos, um por eixo. A imagem treme
    // como uma película mal presa no projetor.
    const vec2 weave = vec2(sin(frame * 0.31 + p.p1.z), cos(frame * 0.27 + p.p1.z * 1.7)) * p.p0.w;
    const vec2 base = inUv + weave * uvPerLayer;

    vec4 src = unpremultiply(texture(u_tex0, base));   // fora da imagem: transparente (amostrador de borda)
    vec3 c = max(src.rgb, vec3(0.0));

    // 3. Piscar: a exposição oscila. Vale para o quadro inteiro.
    if (p.p0.z > 0.0) {
        const float f = (aurea_hash(uvec2(uint(int(frame)) + 101u, uint(p.p1.z) ^ 0x9E37u)) * 2.0 - 1.0)
                      + sin(frame * 1.9) * 0.4;
        c *= 1.0 + f * p.p0.z * 0.35;
    }

    // 1. Poeira: um campo de pontos por quadro. Cada ponto é claro OU escuro
    // (a sujeira tampa, o buraco na emulsão clareia).
    if (p.p0.x > 0.0) {
        const float sizePx = max(p.p2.x, 0.5);
        const vec2 q = (inUv / uvPerLayer) / sizePx;
        const vec2 cell = floor(q);
        const float r = aurea_hash(uvec2(uvec2(ivec2(cell)) ^ uvec2(uint(frame) * 2654435761u, uint(p.p1.z))));
        if (r < p.p0.x * 0.02) {
            const float inside = length(fract(q) - 0.5) * 2.0;
            const float dot01 = smoothstep(1.0, 0.2, inside);
            const float bright = aurea_hash(uvec2(uvec2(ivec2(cell)) + uvec2(17u, 0u)));
            c = mix(c, bright > 0.5 ? c + vec3(0.35) : c * 0.15, dot01);
        }
    }

    // 2. Riscos: linhas verticais finas. Cada risco tem uma posição e uma
    // duração; fora da janela ele não existe.
    if (p.p0.y > 0.0) {
        for (int i = 0; i < 2; ++i) {
            const float idx = float(i);
            const float slot = floor(frame / 6.0) + idx;
            const float on = aurea_hash(uvec2(uint(int(slot)), uint(p.p1.z) ^ 0x51EDu));
            if (on > 1.0 - p.p0.y * 0.5) {
                const float x = aurea_hash(uvec2(uint(int(slot)) + 31u, uint(p.p1.z)));
                const float d = abs(fract(inUv.x - x + 0.5) - 0.5);
                const float wdt = 0.0008 + on * 0.002;
                const float y0 = aurea_hash(uvec2(uint(int(slot)) + 61u, uint(p.p1.z)));
                const float len = clamp(p.p2.y, 0.05, 1.0);
                const float visible = smoothstep(y0 + len, y0, inUv.y) * smoothstep(y0, y0 + 0.02, inUv.y);
                const float line = smoothstep(wdt, 0.0, d) * visible;
                c = mix(c, c * 0.25 + vec3(0.5), line * clamp(p.p0.y * 2.0, 0.0, 1.0));
            }
        }
    }

    // 5. Queimado: a luz entrou pela borda do quadro.
    if (p.p1.x > 0.0) {
        const float d = length((v_uv - vec2(0.5)) * vec2(1.0, 0.75)) * 1.6;
        const float burn = smoothstep(0.55, 1.15, d);
        const vec3 warm = mix(vec3(1.0, 0.42, 0.12), vec3(0.35, 0.6, 1.0), clamp(p.p2.w, 0.0, 1.0));
        c += warm * burn * p.p1.x * 1.6;
        c *= 1.0 + burn * p.p1.x * 0.3;
    }

    // 6. Emenda: a linha de colagem passa de tempos em tempos.
    if (p.p1.y > 0.5) {
        const float y = fract(aurea_hash(uvec2(uint(int(floor(frame / 40.0))), uint(p.p1.z))) + frame * p.p2.z * 0.004);
        const float d = abs(fract(inUv.y - y + 0.5) - 0.5);
        const float splice = smoothstep(0.004, 0.0, d);
        c = mix(c, c * 0.5 + vec3(0.25), splice);
    }

    const float k = clamp(p.p1.w, 0.0, 1.0);
    o_color = premultiply(vec4(max(mix(src.rgb, c, k), vec3(0.0)), src.a));
}

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

void main() {
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

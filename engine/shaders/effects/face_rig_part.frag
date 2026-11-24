#version 450
// =============================================================================
//  Aurea / shaders / effects / face_rig_part.frag
//
//  Parte do Rig 2D separado: a parte amostrada no ponto de repouso.
//   - modo 1 (branco/íris do olho): a pálpebra CORTA o olho acima da linha
//     dela (a pele de baixo já está preenchida na camada do rosto); espelho de
//     `lid_line` em src/effects/FaceRigParts.cpp;
//   - modo 2 (boca): a faixa da linha que estica vira o miolo — escuro, com os
//     dentes em cima e a língua embaixo, só dentro da própria boca.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_rest;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;
    vec4 inputRegion;
    vec4 info;
    vec4 eye;       // centro xy, meia largura, meia altura (px de repouso da parte)
    vec4 lid;       // fechamento, topo e linha de baixo (frações do raio y), modo
    vec4 mouth;     // centro xy, meia largura, meia altura
    vec4 mouthK;    // abertura, topo e base da faixa da linha
    vec4 mouthUv;   // linha da boca (uv)
} p;

vec3 straight(vec4 c) { return c.rgb / max(c.a, 1e-3); }

float lid_line(float x) {
    const vec2 c = p.eye.xy;
    const vec2 s = max(p.eye.zw, vec2(0.5));
    const float top = c.y + p.lid.y * s.y;
    const float low = c.y + max(p.lid.z, p.lid.y + 0.1) * s.y;
    const float u = (x - c.x) / (1.1 * s.x);
    const float uu = min(u * u, 1.0);
    const float arc = top + (c.y - top) * 0.8 * uu - 0.15 * s.y;
    const float lowArc = low - (low - c.y) * 0.3 * uu;
    return arc + clamp(p.lid.x, 0.0, 1.0) * (lowArc - arc);
}

void main() {
    vec4 c = texture(u_tex0, v_uv);
    const float mode = p.lid.w;
    if (mode > 0.5 && mode < 1.5) {
        // Olho fechando: só o que está abaixo da pálpebra aparece; fechado de todo, nada.
        const float k = clamp(p.lid.x, 0.0, 1.0);
        const float line = lid_line(v_rest.x);
        c *= smoothstep(line - 0.6, line + 0.6, v_rest.y) * (1.0 - smoothstep(0.92, 1.0, k));
    } else if (mode > 1.5) {
        const float open = p.mouthK.x;
        if (open > 0.0 && v_rest.y >= p.mouthK.y && v_rest.y <= p.mouthK.z && c.a > 0.0) {
            const float wx = 1.0 - smoothstep(0.7, 1.2, abs(v_rest.x - p.mouth.x) / max(p.mouth.z, 0.5));
            const float t = (v_rest.y - p.mouthK.y) / max(p.mouthK.z - p.mouthK.y, 1e-3);
            const float inner = smoothstep(0.0, 0.2, t) * (1.0 - smoothstep(0.8, 1.0, t));
            const vec3 lineS = straight(texture(u_tex0, p.mouthUv.xy));
            // Miolo escuro avermelhado; dentes no alto da faixa, língua embaixo.
            vec3 inside = mix(lineS * 0.6, vec3(0.30, 0.06, 0.08), 0.6);
            // Dentes e língua só quando a boca abre bem (boca pouco aberta: só o miolo).
            const float wide = smoothstep(0.4, 1.0, open);
            inside = mix(inside, vec3(0.95, 0.93, 0.90), (1.0 - smoothstep(0.05, 0.35, t)) * 0.45 * wide);
            inside = mix(inside, vec3(0.82, 0.38, 0.40), smoothstep(0.55, 0.95, t) * 0.5 * wide);
            const float amt = smoothstep(0.0, 0.3, open) * wx * (0.4 + 0.6 * inner);
            c.rgb = mix(c.rgb, clamp(inside, 0.0, 1.0) * c.a, amt);
        }
    }
    o_color = c;
}

#version 450
// =============================================================================
//  Aurea / shaders / effects / face_rig.frag
//
//  Rig 2D de rosto: a camada amostrada no ponto de repouso. Olho fechando: a
//  faixa de pele acima do olho (que estica por cima dele) puxa para a cor da
//  pele e ganha, embaixo, a linha dos cílios. Boca abrindo: a faixa da linha
//  da boca (que estica) escurece para o miolo. Espelho das linhas de
//  `eye_lines`/`mouth_lines` (src/effects/FaceRig.cpp).
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
    vec4 eye[2];       // centro xy, meia largura, meia altura (px de repouso)
    vec4 eyeK;         // fechamento de cada olho
    vec4 skin[2];      // dois pontos de pele por olho (uv)
    vec4 lash;         // cílios de cima (uv: olho 0 xy, olho 1 zw)
    vec4 mouth;        // centro xy, meia largura, meia altura
    vec4 mouthK;       // abertura, topo e base da faixa da linha
    vec4 mouthUv;      // linha (xy) e bochecha (zw)
    vec4 skinLow[2];   // 1.0.8: dois pontos de pele abaixo de cada olho (uv)
} p;

vec3 straight(vec4 c) { return c.rgb / max(c.a, 1e-3); }

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// A amostra mais clara das quatro: acima do olho do desenho há cílios e franja,
// abaixo pode haver o aro dos óculos; a pele costuma ser a mais clara.
vec3 skin_of(vec4 above, vec4 below) {
    vec3 best = straight(texture(u_tex0, above.xy));
    vec3 s = straight(texture(u_tex0, above.zw));
    if (luma(s) > luma(best)) best = s;
    s = straight(texture(u_tex0, below.xy));
    if (luma(s) > luma(best)) best = s;
    s = straight(texture(u_tex0, below.zw));
    if (luma(s) > luma(best)) best = s;
    return best;
}

vec4 lid(vec4 c, vec4 e, float k, vec4 skinUv, vec4 skinLowUv, vec2 lashUv) {
    if (k <= 0.0) return c;
    const float sy = max(e.w, 0.5);
    const float top = e.y - sy;
    const float band = top - 0.9 * sy;
    const float low = e.y + 0.7 * sy;
    if (v_rest.y <= band || v_rest.y > e.y + 1.1 * sy) return c;
    // Contorno arredondado (não um retângulo): a largura encolhe nas pontas de cima e de baixo.
    const float vy = (v_rest.y - e.y) / (1.9 * sy);
    const float arc = sqrt(max(1.0 - vy * vy, 0.0));
    const float wx = 1.0 - smoothstep(0.8, 1.1, abs(v_rest.x - e.x) / max(e.z * max(arc, 0.35) * 1.3, 0.5));
    if (wx <= 0.0) return c;
    const float kk = min(k, 1.0);
    // Cores sem alfa (o alfa da camada fica): pele e cílio bem escuro.
    const vec3 skinS = skin_of(skinUv, skinLowUv);
    const vec3 lashS = min(straight(texture(u_tex0, lashUv)), skinS * 0.3);
    if (v_rest.y <= top) {
        // Faixa que estica por cima do olho: vira pele lisa (franja e cílios de
        // cima não arrastam), com a linha dos cílios na borda de baixo.
        const float t = (v_rest.y - band) / (top - band);
        c.rgb = mix(c.rgb, skinS * c.a, kk * wx * smoothstep(0.0, 0.2, t));
        const float stretched = (top - band) + kk * (low - top);
        // Cílios em arco (‿): mais baixos no meio, afinando nas pontas.
        const float u = clamp((v_rest.x - e.x) / max(1.2 * e.z, 0.5), -1.0, 1.0);
        const float dt = clamp(0.28 * sy / max(stretched, 1e-3), 0.0, 0.5) * (1.0 - 0.6 * u * u);
        const float line = 1.0 - 0.3 * clamp(0.28 * sy / max(stretched, 1e-3), 0.0, 0.5) * 4.0 * u * u;
        const float lash = smoothstep(line - dt, line - 0.6 * dt, t) * (1.0 - smoothstep(line - 0.2 * dt, line, t));
        c.rgb = mix(c.rgb, lashS * c.a, kk * kk * wx * lash);
        return c;
    }
    // Abaixo da linha da pálpebra fechada (fora da área esticada): o resto do
    // olho (fundo da íris, cílios de baixo) some na pele.
    if (v_rest.y > low) {
        const float under = 1.0 - smoothstep(e.y + 0.85 * sy, e.y + 1.1 * sy, v_rest.y);
        c.rgb = mix(c.rgb, skinS * c.a, kk * kk * wx * under);
    }
    return c;
}

void main() {
    vec4 c = texture(u_tex0, v_uv);
    c = lid(c, p.eye[0], p.eyeK.x, p.skin[0], p.skinLow[0], p.lash.xy);
    c = lid(c, p.eye[1], p.eyeK.y, p.skin[1], p.skinLow[1], p.lash.zw);
    const float open = p.mouthK.x;
    if (open > 0.0 && v_rest.y >= p.mouthK.y && v_rest.y <= p.mouthK.z) {
        const float wx = 1.0 - smoothstep(0.9, 1.5, abs(v_rest.x - p.mouth.x) / max(p.mouth.z, 0.5));
        const float t = (v_rest.y - p.mouthK.y) / max(p.mouthK.z - p.mouthK.y, 1e-3);
        const float inner = smoothstep(0.0, 0.25, t) * (1.0 - smoothstep(0.75, 1.0, t));
        const vec3 lineS = straight(texture(u_tex0, p.mouthUv.xy));
        const vec3 cheekS = straight(texture(u_tex0, p.mouthUv.zw));
        // Miolo: a cor da linha puxada para um vermelho escuro da pele; língua embaixo.
        vec3 inside = mix(lineS, cheekS * vec3(0.5, 0.16, 0.16), 0.6);
        inside = mix(inside, cheekS * vec3(0.75, 0.35, 0.35), smoothstep(0.55, 0.9, t) * 0.6);
        const float amt = smoothstep(0.0, 0.3, open) * wx * (0.35 + 0.65 * inner);
        c.rgb = mix(c.rgb, clamp(inside, 0.0, 1.0) * c.a, amt);
    }
    o_color = c;
}

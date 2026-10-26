#version 450
// =============================================================================
//  Aurea / shaders / hyper / mi_interp.frag
//
//  Hyper Motion (Turbo): o quadro no instante t entre A e B pelos vetores de
//  bloco nos dois sentidos (me_search/me_pack: RG = A -> B, BA = B -> A).
//  Código próprio (não é o minterpolate do FFmpeg).
//
//  1. Candidatos (vetores A -> B): o campo bilinear nos dois sentidos (o
//     B -> A entra negado) e os 4 blocos mais perto nos dois sentidos.
//  2. Para cada candidato v: o pixel p em t veio de A em p − t·v e vai para B
//     em p + (1 − t)·v; o erro é a diferença de luma entre as duas pontas
//     numa cruz de 5 amostras. Fica o de menor erro (fronteiras nítidas: o
//     pixel escolhe o movimento do objeto OU o do fundo, não a média).
//  3. Oclusão pela ida e volta do campo: o ponto de A cujo vetor não volta
//     (F(a) + G(a + F(a)) longe de 0) some em B (está sendo coberto) e só A
//     vale; o ponto de B cujo vetor não volta aparece agora e só B vale.
//  4. Sem nenhum lado confiável (erro alto nas duas pontas, corte de cena):
//     o quadro mais perto no tempo, em vez de misturar dois fantasmas.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_a;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_b;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_field;   // RG = A->B, BA = B->A
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_luma;    // R = A, G = B (perceptual)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;    // xy = tamanho (px), zw = 1/xy
    vec4 field;   // x = bloco (px), yz = fw, fh, w = t
    vec4 tune;    // x = erro aceitável (luma perceptual), y = sigma da ida e volta (px)
} p;

vec4 field_at(vec2 pos) { return textureLod(u_field, pos / (p.field.x * p.field.yz), 0.0); }
float la(vec2 pos) { return textureLod(u_luma, pos * p.size.zw, 0.0).r; }
float lb(vec2 pos) { return textureLod(u_luma, pos * p.size.zw, 0.0).g; }

float match(vec2 pos, vec2 v, float t) {
    const vec2 pa = pos - t * v, pb = pos + (1.0 - t) * v;
    float e = abs(la(pa) - lb(pb));
    e += abs(la(pa + vec2(1.5, 0.0)) - lb(pb + vec2(1.5, 0.0)));
    e += abs(la(pa - vec2(1.5, 0.0)) - lb(pb - vec2(1.5, 0.0)));
    e += abs(la(pa + vec2(0.0, 1.5)) - lb(pb + vec2(0.0, 1.5)));
    e += abs(la(pa - vec2(0.0, 1.5)) - lb(pb - vec2(0.0, 1.5)));
    return e * 0.2;
}

void main() {
    const vec2 pos = gl_FragCoord.xy;
    const float t = p.field.w;
    const ivec2 fsz = ivec2(p.field.yz);
    const vec4 bil = field_at(pos);
    vec2 cand[10];
    cand[0] = bil.rg;
    cand[1] = -bil.ba;
    const ivec2 c0 = ivec2(floor(pos / p.field.x - 0.5));
    int k = 2;
    for (int j = 0; j <= 1; ++j)
        for (int i = 0; i <= 1; ++i) {
            const vec4 f = texelFetch(u_field, clamp(c0 + ivec2(i, j), ivec2(0), fsz - 1), 0);
            cand[k++] = f.rg;
            cand[k++] = -f.ba;
        }
    vec2 v = cand[0];
    float best = 1e9;
    for (int i = 0; i < 10; ++i) {
        // Leve preferência pelo campo suave (desempate em área lisa).
        const float e = match(pos, cand[i], t) + 0.002 * length(cand[i] - bil.rg);
        if (e < best) { best = e; v = cand[i]; }
    }
    const vec2 pa = pos - t * v, pb = pos + (1.0 - t) * v;
    const vec4 ca = textureLod(u_a, pa * p.size.zw, 0.0);
    const vec4 cb = textureLod(u_b, pb * p.size.zw, 0.0);
    // Ida e volta (px): F(a) + G(a + F(a)) e G(b) + F(b + G(b)).
    const vec2 Fa = field_at(pa).rg;
    const float oa = length(Fa + field_at(pa + Fa).ba);
    const vec2 Gb = field_at(pb).ba;
    const float ob = length(Gb + field_at(pb + Gb).rg);
    const float so = max(p.tune.y, 0.5);
    const float keepA = exp(-(oa * oa) / (so * so));   // 1 = o ponto de A existe em B
    const float keepB = exp(-(ob * ob) / (so * so));
    float wa = (1.0 - t) * keepA;
    float wb = t * keepB;
    if (wa + wb < 1e-3) { wa = 1.0 - t; wb = t; }
    const vec4 mixed = (wa * ca + wb * cb) / (wa + wb);
    // As duas pontas não casam: se só um lado está ocluído (coberto/aparecendo)
    // vale o outro; senão (corte, transparência, erro do campo) o quadro mais
    // perto no tempo — nunca a mistura de dois fantasmas.
    const float lost = smoothstep(p.tune.x, 2.5 * p.tune.x, best);
    const float onlyA = keepA * (1.0 - keepB), onlyB = keepB * (1.0 - keepA);
    const vec4 nearest = t < 0.5 ? texelFetch(u_a, ivec2(pos), 0) : texelFetch(u_b, ivec2(pos), 0);
    const vec4 oneSided = onlyA + onlyB > 0.2 ? (onlyA * ca + onlyB * cb) / (onlyA + onlyB) : nearest;
    o_color = mix(mixed, oneSided, lost);
}

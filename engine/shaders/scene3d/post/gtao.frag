#version 450
// =============================================================================
//  Aurea / shaders / scene3d / post / gtao.frag
//
//  Oclusão ambiente em espaço de tela (0.4.6, "realismo nível Blender"): a
//  busca de horizonte do GTAO (Jimenez et al., "Practical Real-Time
//  Strategies for Accurate Indirect Occlusion", 2016) numa versão compacta
//  para celular. Lê a profundidade do pré-passe (Z reverso infinito, meia
//  resolução), reconstrói a posição de vista e a normal pela própria
//  profundidade e, em `cfg.z` direções girando por pixel (ruído IGN), acha o
//  ângulo do horizonte dos dois lados até o raio. A visibilidade é a
//  integral do cosseno entre os horizontes (fórmula fechada do GTAO).
//
//  Saída: R = AO (1 = aberto), G = profundidade de vista (para o desfoque
//  bilateral). Céu (profundidade 0) = 1.
// =============================================================================
#include "../../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_ao;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D t_depth;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    mat4 invProj;   // vista <- clip (a projeção do passe, com a transformação da imagem)
    vec4 texel;     // xy = 1/tamanho, zw = tamanho (meia resolução)
    vec4 cfg;       // x = raio na tela (fração da altura), y = força, z = direções, w = passos por lado
    vec4 extra;     // x = queda (0..1 do raio onde começa a sumir), y = viés do cosseno
} p;

const float PI = 3.14159265359;

vec3 view_pos(vec2 uv, float d) {
    vec4 v = p.invProj * vec4(uv * 2.0 - 1.0, d, 1.0);
    return v.xyz / v.w;
}

vec3 view_at(vec2 uv) {
    float d = textureLod(t_depth, uv, 0.0).r;
    return view_pos(uv, max(d, 1e-7));
}

void main() {
    float d = textureLod(t_depth, v_uv, 0.0).r;
    if (d <= 0.0) { o_ao = vec4(1.0, 0.0, 0.0, 1.0); return; }   // céu: nada oclui
    vec3 P = view_pos(v_uv, d);
    float z = abs(P.z);

    // Normal pela profundidade: de cada eixo, o vizinho mais próximo em Z
    // (não atravessa a borda de um objeto para o fundo).
    vec2 tx = vec2(p.texel.x, 0.0), ty = vec2(0.0, p.texel.y);
    vec3 pl = view_at(v_uv - tx), pr = view_at(v_uv + tx);
    vec3 pu = view_at(v_uv - ty), pd = view_at(v_uv + ty);
    vec3 dx = abs(pr.z - P.z) < abs(P.z - pl.z) ? pr - P : P - pl;
    vec3 dy = abs(pd.z - P.z) < abs(P.z - pu.z) ? pd - P : P - pu;
    vec3 N = normalize(cross(dy, dx));
    vec3 V = normalize(-P);
    if (dot(N, V) < 0.0) N = -N;

    // Raio: fração fixa da tela (o mesmo efeito em cena de pixels ou de metros),
    // convertida para o mundo nesta profundidade.
    float radiusPx = max(p.cfg.x * p.texel.w, 2.0);
    float pxToWorld = length(view_pos(v_uv + vec2(p.texel.x, 0.0), d) - P);
    float R = radiusPx * pxToWorld;
    int dirs = int(p.cfg.z + 0.5);
    int steps = int(p.cfg.w + 0.5);

    // Ruído de gradiente intercalado: gira as direções e desloca os passos
    // por pixel (o desfoque bilateral limpa o padrão).
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float jitter = fract(noise * 7.137);

    float visibility = 0.0;
    for (int i = 0; i < dirs; ++i) {
        float phi = (float(i) + noise) * PI / float(dirs);
        vec2 dir = vec2(cos(phi), sin(phi));
        // Plano da fatia: a direção na tela levada à vista EXATAMENTE (um
        // ponto vizinho na mesma profundidade pela inversa da projeção) — o
        // sinal do ângulo da normal tem que bater com o lado das amostras.
        vec3 stepView = view_pos(v_uv + dir * p.texel.xy, d) - P;
        vec3 sliceDir = normalize(stepView - V * dot(stepView, V));
        vec3 axis = normalize(cross(sliceDir, V));
        vec3 projN = N - axis * dot(N, axis);
        float projLen = length(projN);
        if (projLen < 1e-4) { visibility += 1.0; continue; }
        float cosN = clamp(dot(projN, V) / projLen, -1.0, 1.0);
        float n = sign(dot(projN, sliceDir)) * acos(cosN);   // ângulo da normal na fatia
        float h[2];
        for (int side = 0; side < 2; ++side) {
            float s = side == 0 ? 1.0 : -1.0;
            float maxCos = -1.0;
            for (int k = 0; k < steps; ++k) {
                float t = (float(k) + jitter) / float(steps);
                t = t * t;                                         // mais amostras perto
                vec2 off = s * dir * (1.0 + t * radiusPx) * p.texel.xy;
                vec2 suv = v_uv + off;
                if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) break;
                float sd = textureLod(t_depth, suv, 0.0).r;
                if (sd <= 0.0) continue;
                vec3 S = view_pos(suv, sd) - P;
                float dist = length(S);
                float c = dot(S / max(dist, 1e-6), V);
                // Queda: amostra além do raio deixa de ocluir (sem halo no fundo).
                float fall = clamp((R - dist) / max(R * (1.0 - p.extra.x), 1e-6), 0.0, 1.0);
                maxCos = max(maxCos, mix(-1.0, c, fall));
            }
            h[side] = s * acos(clamp(maxCos, -1.0, 1.0));
        }
        // Horizontes presos ao hemisfério da normal; integral do cosseno.
        float h0 = n + max(h[1] - n, -PI * 0.5);
        float h1 = n + min(h[0] - n, PI * 0.5);
        float a = 0.25 * (-cos(2.0 * h0 - n) + cosN + 2.0 * h0 * sin(n))
                + 0.25 * (-cos(2.0 * h1 - n) + cosN + 2.0 * h1 * sin(n));
        visibility += projLen * a;
    }
    visibility = clamp(visibility / float(max(dirs, 1)), 0.0, 1.0);
    float ao = pow(visibility, max(p.cfg.y, 0.0));
    o_ao = vec4(ao, z, 0.0, 1.0);
}

#version 450
// =============================================================================
//  Aurea / shaders / scene3d / motion / reconstruct.frag
//
//  Motion blur 3D rápido, a RECONSTRUÇÃO: cada pixel X é a média, ao longo do
//  obturador, do que passa por ele. As amostras caem na reta da maior
//  velocidade da vizinhança (neighbor_max.frag), centradas em X e cobrindo o
//  rastro inteiro (−½ … +½ do deslocamento): a amostra Y em X + v·t é o que
//  estaria em X no instante −t. Ela entra com a probabilidade a ∈ [0, 1] de
//  isso ser verdade (raio = meio rastro, px); o resto da amostra é a própria
//  cor de X:
//    • Y NA FRENTE de X: entra se o rastro de Y alcança X (o objeto que anda
//      espalha sobre o fundo, inclusive para fora da própria silhueta);
//    • Y ATRÁS de X: entra se o rastro de X alcança Y (o fundo aparece através
//      da borda borrada de X);
//    • mesma superfície: entra se os dois rastros se alcançam.
//  Com uma velocidade só, isso é EXATAMENTE a média de caixa da acumulação
//  (rampa linear de 2 raios em cada borda). Um X parado (raio ≤ ½ px) na
//  frente de um Y que anda não recebe nada de Y: o fundo em movimento NÃO vaza
//  sobre o objeto parado na frente. "Na frente" é por distância (do Z
//  reverso) com uma faixa relativa macia.
//
//  Cena HDR (codificada para o resolve) e 2D de exibição recebem os MESMOS
//  pesos; a cor é lida pontual (o bilinear misturaria cor de um com a
//  velocidade de outro). Amostras: proporcionais ao rastro, no teto do
//  parâmetro (o mesmo no preview e no export); o deslocamento por pixel é um
//  ruído determinístico (gradiente intercalado) — igual em toda execução.
// =============================================================================
#include "../../common/bindings.glsl"
#include "../common/hdr.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_display;
layout(location = 1) out vec4 o_scene;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_scene;     // HDR codificado (resolvido)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_display;   // 2D de exibição (resolvido)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_velocity;  // xy px, z = Z reverso, w = superfície
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_neighbor;  // maior velocidade da vizinhança (por bloco)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 cfg;     // x = K (lado do bloco, px), y = teto de amostras, zw = tamanho do alvo
    vec4 shape;   // x = faixa macia de profundidade (relativa), y = amostras por px de rastro
} p;

float distance_of(float reverseZ) { return 1.0 / max(reverseZ, 1e-6); }
// 1 quando a está na frente de b (ou na mesma superfície), 0 quando bem atrás.
float closer(float a, float b) { return clamp(1.0 - (a - b) / (p.shape.x * min(a, b)), 0.0, 1.0); }
// O rastro de raio r alcança a distância d (borda macia de ±5%).
float reach(float d, float r) { return 1.0 - smoothstep(0.95 * r, 1.05 * r, d); }

void main() {
    const ivec2 size = ivec2(p.cfg.zw + 0.5);
    const ivec2 px = ivec2(gl_FragCoord.xy);
    const int k = max(int(p.cfg.x + 0.5), 1);
    const vec4 sc0 = texelFetch(u_scene, px, 0);
    const vec4 dc0 = texelFetch(u_display, px, 0);
    const vec2 vn = texelFetch(u_neighbor, px / k, 0).xy;
    const float lenN = length(vn);
    // Rastro de menos de 1 px na vizinhança: nada a reconstruir.
    if (!(lenN >= 1.0)) {
        o_scene = sc0;
        o_display = dc0;
        return;
    }
    const vec4 vx = texelFetch(u_velocity, px, 0);
    const float rX = max(0.5, 0.5 * length(vx.xy));
    const float zX = distance_of(vx.z);
    const int taps = clamp(int(ceil(lenN * p.shape.y)), 2, max(int(p.cfg.y + 0.5), 2));
    const vec4 cX = vec4(aurea_hdr_decode(sc0.rgb), sc0.a);
    vec4 accS = vec4(0.0);
    vec4 accD = vec4(0.0);
    float taken = 0.0;
    const float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) - 0.5;
    for (int i = 0; i < taps; ++i) {
        const float t = (float(i) + 0.5 + jitter) / float(taps) - 0.5;
        const vec2 offset = vn * t;
        const float d = length(offset);
        if (d < 0.5) continue;   // o próprio pixel: a cor de X
        const ivec2 py = clamp(ivec2(floor(gl_FragCoord.xy + offset)), ivec2(0), size - 1);
        const vec4 vy = texelFetch(u_velocity, py, 0);
        const float rY = max(0.5, 0.5 * length(vy.xy));
        const float zY = distance_of(vy.z);
        const float f = closer(zY, zX), b = closer(zX, zY);
        const float same = f * b;
        const float a = (f - same) * reach(d, rY) + (b - same) * reach(d, rX) + same * reach(d, rY) * reach(d, rX);
        if (a <= 0.0) continue;
        const vec4 ss = texelFetch(u_scene, py, 0);
        accS += vec4(aurea_hdr_decode(ss.rgb), ss.a) * a;
        accD += texelFetch(u_display, py, 0) * a;
        taken += a;
    }
    const float n = float(taps);
    accS = (accS + cX * (n - taken)) / n;
    accD = (accD + dc0 * (n - taken)) / n;
    vec3 hdr = accS.rgb;
    if (any(isnan(hdr)) || any(isinf(hdr))) hdr = aurea_hdr_decode(sc0.rgb);
    o_scene = vec4(aurea_hdr_encode(hdr), clamp(accS.a, 0.0, 1.0));
    o_display = vec4(max(accD.rgb, vec3(0.0)), clamp(accD.a, 0.0, 1.0));
}

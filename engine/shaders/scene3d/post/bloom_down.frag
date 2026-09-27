#version 450
// =============================================================================
//  Aurea / shaders / scene3d / post / bloom_down.frag
//
//  Descida da cadeia do bloom (Jimenez, "Next Generation Post Processing in
//  Call of Duty: Advanced Warfare", 2014): 13 amostras bilineares = 36 texels
//  da origem em 5 caixas 4×4 sobrepostas (centro com peso 0,5, cantos 0,125).
//  Sem o serrilhado e o "pulsar" do downsample de 4 amostras quando a câmera
//  anda.
//
//  PRIMEIRO nível (`texel.z` = 1): lê a cena HDR resolvida, decodifica,
//  aplica a exposição do grupo e o limiar suave (joelho quadrático), e pondera
//  cada caixa pela média de Karis (1/(1+luma)) — um único texel de 1000 (um
//  reflexo especular num pixel) não vira um quadrado piscando no bloom.
// =============================================================================
#include "../../common/bindings.glsl"
#include "../common/hdr.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho da ORIGEM, z = 1 no primeiro nível, w = limiar (0 = sem)
    vec4 cfg;     // x = joelho do limiar, y = exposição do grupo
} p;

vec3 tap(vec2 offset) {
    vec3 c = textureLod(u_src, v_uv + offset * p.texel.xy, 0.0).rgb;
    if (p.texel.z > 0.5) c = aurea_hdr_decode(c) * p.cfg.y;
    return c;
}

vec3 karis(vec3 a, vec3 b, vec3 c, vec3 d, out float w) {
    const vec3 m = (a + b + c + d) * 0.25;
    w = p.texel.z > 0.5 ? 1.0 / (1.0 + aurea_luma(m)) : 1.0;
    return m;
}

void main() {
    const vec3 a = tap(vec2(-2.0, -2.0)), b = tap(vec2(0.0, -2.0)), c = tap(vec2(2.0, -2.0));
    const vec3 d = tap(vec2(-1.0, -1.0)), e = tap(vec2(1.0, -1.0));
    const vec3 f = tap(vec2(-2.0, 0.0)), g = tap(vec2(0.0, 0.0)), h = tap(vec2(2.0, 0.0));
    const vec3 i = tap(vec2(-1.0, 1.0)), j = tap(vec2(1.0, 1.0));
    const vec3 k = tap(vec2(-2.0, 2.0)), l = tap(vec2(0.0, 2.0)), m = tap(vec2(2.0, 2.0));

    float w0, w1, w2, w3, w4;
    const vec3 g0 = karis(d, e, i, j, w0);
    const vec3 g1 = karis(a, b, f, g, w1);
    const vec3 g2 = karis(b, c, g, h, w2);
    const vec3 g3 = karis(f, g, k, l, w3);
    const vec3 g4 = karis(g, h, l, m, w4);
    w0 *= 0.5;
    w1 *= 0.125; w2 *= 0.125; w3 *= 0.125; w4 *= 0.125;
    vec3 sum = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);

    if (p.texel.z > 0.5 && p.texel.w > 0.0) {
        // Limiar suave (curva quadrática no joelho): sem a borda dura que
        // recorta o halo onde o brilho cruza o limiar.
        const float threshold = p.texel.w;
        const float knee = max(p.cfg.x, 1e-4);
        const float br = max(sum.r, max(sum.g, sum.b));
        float rq = clamp(br - threshold + knee, 0.0, 2.0 * knee);
        rq = rq * rq / (4.0 * knee);
        sum *= max(rq, br - threshold) / max(br, 1e-4);
    }
    // fp16 aguenta 65504; um pixel quebrado (Inf) não contamina a cadeia.
    sum = clamp(sum, vec3(0.0), vec3(60000.0));
    if (any(isnan(sum))) sum = vec3(0.0);
    o_color = vec4(sum, 0.0);
}

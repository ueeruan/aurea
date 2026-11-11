#version 450
// =============================================================================
//  Aurea / shaders / effects / glow_octave_composite.frag
//
//  Brilho em oitavas: a composição.
//
//  * Exposição multiplica a luz somada; a cor (com a quantidade) só TIRA luz
//    de um canal — um tingimento que clareasse viraria uma segunda exposição.
//  * Mapa de tom 1 - e^(-x), nunca um corte: seis oitavas somadas passam do
//    branco perto da fonte, e um corte deixaria um platô com aro duro no meio
//    do decaimento. No modo somar, a luz é só limitada a 1.
//  * Sobre a layer, o brilho entra em "tela" no valor CODIFICADO da cor; fora
//    dela, é luz própria cuja cobertura é o próprio brilho (linear) — a menor
//    que carrega a luz: o rabo do halo clareia o fundo em vez de escurecê-lo.
//  * Sai no linear pré-multiplicado do motor.
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // entrada
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // oitavas somadas

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;    // uv da entrada = v_uv * xy + zw
    vec4 texel;    // uv do brilho  = v_uv * xy + zw
    vec4 p0;       // x = exposição, y = quantidade da cor, z = aberração, w = modo somar
    vec4 p1;       // x = só o brilho, y = ganho da pirâmide de um nível só,
                   // z = compor o original (0 na frente, 1 atrás, 2 nenhum), w = 1 cores A e B (AE 2026)
    vec4 p2;       // cores A e B: x = repetições, y = fase (voltas), z = ponto médio, w = repetição (0..3)
    vec4 p3;       // cor A (linear)
    vec4 color;    // cor do brilho (linear)
    vec4 colorB;   // cor B (linear)
} p;

/// Cores A e B: a intensidade do brilho percorre o degradê A-B pela repetição escolhida.
vec3 ab_color(float t) {
    float x = fract(t * max(p.p2.x, 1.0) + p.p2.y);
    const int looping = int(p.p2.w + 0.5);
    if (looping == 1) x = 1.0 - x;
    else if (looping == 2) x = 1.0 - abs(2.0 * x - 1.0);
    else if (looping == 3) x = abs(2.0 * x - 1.0);
    const float m = clamp(p.p2.z, 0.01, 0.99);
    x = x < m ? 0.5 * x / m : 0.5 + 0.5 * (x - m) / (1.0 - m);
    return mix(max(p.p3.rgb, vec3(0.0)), max(p.colorB.rgb, vec3(0.0)), x);
}

vec2 glow_uv(vec2 uv) { return uv * p.texel.xy + p.texel.zw; }

void main() {
    vec4 g = texture(u_tex1, glow_uv(v_uv)) * p.p1.y;
    if (p.p0.z > 0.001) {
        // Separação radial dos canais a partir do centro: a franja colorida
        // de um brilho grande.
        vec2 d = v_uv - 0.5;
        float ca = p.p0.z * 0.04;
        g.r = texture(u_tex1, glow_uv(0.5 + d * (1.0 + ca))).r * p.p1.y;
        g.b = texture(u_tex1, glow_uv(0.5 + d * (1.0 - ca))).b * p.p1.y;
    }
    vec3 light = g.rgb * p.p0.x;
    if (p.p1.w > 0.5) {
        // Cores A e B: a luz vira a cor do degradê na intensidade dela (A no
        // miolo forte, B no rabo, com a repetição escolhida).
        const float lum = max(dot(light, vec3(0.2126, 0.7152, 0.0722)), 0.0);
        light = ab_color(1.0 - exp(-lum)) * lum;
    } else {
        light *= mix(vec3(1.0), max(p.color.rgb, vec3(0.0)), clamp(p.p0.y, 0.0, 1.0));
    }
    light = max(light, vec3(0.0));
    bool add = p.p0.w > 0.5;
    vec3 toned = add ? min(light, vec3(1.0)) : vec3(1.0) - exp(-light);
    vec3 glowG = linear_to_srgb(toned);

    // A layer recebe o brilho em "tela" no valor CODIFICADO da cor dela (o
    // desenho do app de referência); fora dela, o brilho é luz própria com
    // cobertura linear. Com brilho zero, a saída é a entrada, bit a bit no
    // sentido da cor: o contorno suave da layer não muda.
    vec4 base = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    if (p.p1.x > 0.5 || p.p1.z > 1.5) base = vec4(0.0);   // só o brilho / compor: nenhum
    if (p.p1.z > 0.5 && p.p1.z < 1.5) {
        // Atrás: a camada por cima do brilho, sem tela sobre ela.
        const float covB = max(toned.r, max(toned.g, toned.b));
        o_color = base + vec4(toned, covB) * (1.0 - clamp(base.a, 0.0, 1.0));
        return;
    }
    float a = clamp(base.a, 0.0, 1.0);
    vec3 straight = a > 1e-6 ? max(base.rgb / a, vec3(0.0)) : vec3(0.0);
    vec3 encoded = linear_to_srgb(straight);
    vec3 lit = add ? max(encoded, min(encoded + glowG, vec3(1.0)))
                   : encoded + glowG * (vec3(1.0) - min(encoded, vec3(1.0)));
    vec3 onLayer = srgb_to_linear(lit);
    float cov = max(toned.r, max(toned.g, toned.b));
    o_color = vec4(onLayer * a + toned * (1.0 - a), clamp(a + cov * (1.0 - a), 0.0, 1.0));
}

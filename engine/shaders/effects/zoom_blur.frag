#version 450
// =============================================================================
//  Aurea / shaders / effects / zoom_blur.frag
//
//  Desfoque de zoom: cada pixel é a média de várias amostras tiradas ao longo
//  da RETA que liga o pixel ao centro. É o rastro de "empurrar a lente" — as
//  bordas correm mais que o centro, porque a distância percorrida é
//  proporcional à distância até o centro (o mesmo que um zoom de verdade faz).
//
//  `amount` é a fração da distância ao centro que o rastro cobre: 0.2 dá um
//  risco curto, 1.0 atravessa o quadro até o centro. O sinal troca o sentido
//  (afastar/aproximar).
//
//  Amostras em passo uniforme, presas à textura: o número de taps é fixo, o
//  custo não depende do `amount`.
//
//  Modo (0.4.5, o "Zoom" do CC Radial Fast Blur): 0 = média; 1 = Mais claro
//  (o máximo de cada canal no rastro: as luzes se espalham); 2 = Mais escuro
//  (o mínimo: as sombras se espalham).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x,y = centro em uv, z = intensidade, w = reservado
    vec4 p1;   // x = mistura, y = modo, z = separação RGB, w = amostragem adaptativa
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

const int kTaps = 24;
const int kChromaticTaps = 12;

vec4 chromatic_trail(vec2 uv, vec2 delta, float separation) {
    int taps = kChromaticTaps;
    if (p.p1.w > 0.5) {
        float spanInPixels = length(delta / max(p.texel.xy, vec2(0.000001))) * (1.0 + separation);
        taps = int(clamp(ceil(spanInPixels / 5.0), 4.0, float(kChromaticTaps)));
    }
    vec3 colors = vec3(0.0);
    vec3 alpha = vec3(0.0);
    for (int i = 0; i < kChromaticTaps; ++i) {
        if (i >= taps) break;
        float t = (float(i) + 0.5) / float(taps) - 0.5;
        vec2 stepUV = delta * t;
        vec4 r = texture(u_tex0, uv + stepUV * (1.0 + separation));
        vec4 g = texture(u_tex0, uv + stepUV);
        vec4 b = texture(u_tex0, uv + stepUV * (1.0 - separation));
        colors += vec3(r.r, g.g, b.b);
        alpha += vec3(r.a, g.a, b.a);
    }
    // Keep every channel's colored trail in premultiplied space. The widest
    // alpha footprint covers the separated color without clipping HDR.
    return vec4(colors, max(alpha.r, max(alpha.g, alpha.b))) / float(taps);
}

void main() {
    const vec2 uv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec2 centre = p.p0.xy;
    const float amount = p.p0.z;
    if (p.p1.z > 0.0) {
        vec4 trail = chromatic_trail(uv, (uv - centre) * amount, p.p1.z);
        o_color = mix(texture(u_tex0, uv), trail, clamp(p.p1.x, 0.0, 1.0));
        return;
    }

    const int mode = int(p.p1.y + 0.5);
    vec4 sum = vec4(0.0);
    vec4 hi = vec4(0.0);
    vec4 lo = vec4(1e30);
    for (int i = 0; i < kTaps; ++i) {
        // -0.5 .. 0.5 pelo rastro, no centro de cada amostra.
        const float t = (float(i) + 0.5) / float(kTaps) - 0.5;
        const vec4 c = texture(u_tex0, uv + (uv - centre) * (amount * t));
        sum += c;
        hi = max(hi, c);
        lo = min(lo, c);
    }
    const vec4 trail = mode == 1 ? hi : (mode == 2 ? lo : sum / float(kTaps));
    // O sampler decide a borda (repetir ou esticar): o shader não prende o uv,
    // senão o modo de borda escolhido na interface não teria efeito nenhum.
    o_color = mix(texture(u_tex0, uv), trail, clamp(p.p1.x, 0.0, 1.0));
}

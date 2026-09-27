#version 450
// =============================================================================
//  Aurea / shaders / effects / radial_blur.frag
//
//  Desfoque radial: GIRO (as amostras correm num arco em volta do centro) ou
//  ZOOM (correm na reta que sai do centro). As amostras ficam CENTRADAS no
//  pixel (metade de cada lado), então o centro do giro/zoom não sai do lugar
//  e o desfoque não "empurra" a imagem.
//
//  Contas no plano da camada (px da resolução cheia): a região de saída pode
//  ser maior que a de entrada — o rastro de um zoom passa da caixa da camada.
//  Fora da entrada, o sampler devolve transparente (borda sem esticar).
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = tipo (0 giro, 1 zoom), y = arco total (rad) ou fração do zoom, z = amostras, w = mistura
    vec4 p1;   // xy = centro (px da camada)
    vec4 p2;   // região de SAÍDA (px da camada): x, y, largura, altura
    vec4 p3;   // região de ENTRADA (px da camada)
    vec4 color;
} p;

vec2 to_input(vec2 layerPoint) { return (layerPoint - p.p3.xy) / max(p.p3.zw, vec2(1e-3)); }

void main() {
    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const vec4 center = texture(u_tex0, to_input(point));
    const vec2 c = p.p1.xy;
    const vec2 rel = point - c;
    const bool zoom = p.p0.x > 0.5;
    const int samples = int(clamp(p.p0.z, 2.0, 64.0));

    vec4 sum = vec4(0.0);
    for (int i = 0; i < 64; ++i) {
        if (i >= samples) break;
        const float s = (float(i) + 0.5) / float(samples) - 0.5;   // -0.5..0.5
        vec2 q;
        if (zoom) {
            q = c + rel * max(1.0 - s * p.p0.y, 0.0);
        } else {
            // aurea_rot2(a) gira por -a: o sinal não importa, o arco é simétrico.
            q = c + aurea_rot2(s * p.p0.y) * rel;
        }
        sum += texture(u_tex0, to_input(q));
    }
    const vec4 blurred = sum / float(samples);
    o_color = mix(center, blurred, clamp(p.p0.w, 0.0, 1.0));
}

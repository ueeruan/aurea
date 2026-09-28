#version 450
// =============================================================================
//  Aurea / shaders / effects / depth_map.frag
//
//  Mapa de profundidade (IA): u_tex1 é a disparidade 256×256 da fonte da
//  camada (R, maior = mais perto), já normalizada pelos percentis do próprio
//  quadro; p1.xy são os limites suavizados no tempo, no mesmo espaço. O
//  filtro bilinear do sampler amplia o mapa até a resolução da camada.
//
//  O cinza é um valor CODIFICADO (o que a pessoa vê e o que um mapa de
//  profundidade exportado contém): perto = 1 vira branco na tela. A camada
//  mantém o alfa dela; `mistura` volta ao original.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a disparidade 256×256

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;      // x = mistura 0..1, y = inverter, z = 1 há mapa
    vec4 p1;      // x = limite de longe, y = limite de perto (espaço da textura)
    vec4 p2;      // região da imagem (px da camada)
    vec4 p3;      // xy = tamanho natural da camada (px) — o que o mapa cobre
    vec4 color;
} p;

void main() {
    const vec4 base = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    if (p.p0.z < 0.5) {
        o_color = base;
        return;
    }
    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const vec2 uv = clamp(point / max(p.p3.xy, vec2(1.0)), vec2(0.0), vec2(1.0));
    const float raw = texture(u_tex1, uv).r;
    float d = clamp((raw - p.p1.x) / max(p.p1.y - p.p1.x, 1e-5), 0.0, 1.0);
    if (p.p0.y > 0.5) d = 1.0 - d;
    const vec3 gray = vec3(aurea_srgb_to_linear(d));
    const vec4 mapped = vec4(gray * base.a, base.a);
    o_color = mix(base, mapped, clamp(p.p0.x, 0.0, 1.0));
}

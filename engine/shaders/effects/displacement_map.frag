#version 450
// =============================================================================
//  Aurea / shaders / effects / displacement_map.frag
//
//  Mapa de deslocamento: os pixels andam conforme o valor de um canal do mapa.
//
//  Valor do canal (codificado, 0..1): 0,5 = parado, 1 = +máximo, 0 = −máximo.
//  O pixel da saída em P mostra a entrada em P − deslocamento: com máximo
//  positivo, o branco empurra a imagem para a direita (horizontal) e para
//  baixo (vertical). Onde o mapa é transparente, o valor tende ao neutro (a
//  parte vazia de uma camada-mapa não arrasta nada) — menos no canal Alfa.
//
//  O mapa (u_tex1) é a outra camada desenhada no quadro da composição
//  (`c.zw` px), posta sobre a camada 1 px : 1 px — centralizada, esticada na
//  caixa da camada ou repetida. Sem camada (`b.w` = 0), o mapa é a própria
//  entrada, no mesmo pixel.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // o mapa

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 outRegion;   // região da saída em px da camada
    vec4 inRegion;    // região da entrada em px da camada
    vec4 a;           // x = máx. horizontal (px), y = máx. vertical (px), z = canal H, w = canal V (0 R, 1 G, 2 B, 3 luma, 4 alfa, 5 desligado)
    vec4 b;           // x = comportamento (0 centralizar, 1 esticar, 2 repetir), y = borda (0 repetir pixels, 1 envolver), z = mistura, w = 1 mapa de camada
    vec4 c;           // xy = tamanho da camada (px), zw = tamanho do mapa (px)
} p;

vec2 in_uv(vec2 layerPx) { return (layerPx - p.inRegion.xy) / p.inRegion.zw; }
bool inside01(vec2 uv) { return uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0; }

float channel(int ch, vec3 enc, float a) {
    if (ch == 4) return a;
    float v = 0.5;
    if (ch == 0) v = enc.r;
    else if (ch == 1) v = enc.g;
    else if (ch == 2) v = enc.b;
    else if (ch == 3) v = dot(enc, vec3(0.2126, 0.7152, 0.0722));
    return mix(0.5, v, a);
}

void main() {
    const vec2 px = p.outRegion.xy + v_uv * p.outRegion.zw;
    const vec2 uv0 = in_uv(px);
    const bool here = inside01(uv0);
    const vec4 original = here ? texture(u_tex0, uv0) : vec4(0.0);

    // O mapa neste pixel.
    vec4 m;
    float valid = 1.0;
    if (p.b.w < 0.5) {
        m = texture(u_tex0, clamp(uv0, 0.0, 1.0));
    } else {
        const vec2 layerSize = max(p.c.xy, vec2(1.0));
        const vec2 mapSize = max(p.c.zw, vec2(1.0));
        const int behavior = int(p.b.x + 0.5);
        vec2 muv;
        if (behavior == 0) {
            muv = (px - layerSize * 0.5 + mapSize * 0.5) / mapSize;
            valid = inside01(muv) ? 1.0 : 0.0;
        } else if (behavior == 1) {
            muv = px / layerSize;
        } else {
            muv = fract(px / mapSize);
        }
        m = texture(u_tex1, clamp(muv, 0.0, 1.0));
    }
    const vec4 ms = unpremultiply(m);
    const vec3 enc = aurea_linear_to_srgb(max(ms.rgb, vec3(0.0)));
    const int chH = int(p.a.z + 0.5), chV = int(p.a.w + 0.5);
    const float vh = mix(0.5, channel(chH, enc, ms.a), valid);
    const float vv = mix(0.5, channel(chV, enc, ms.a), valid);
    vec2 d = vec2((vh - 0.5) * 2.0 * p.a.x, (vv - 0.5) * 2.0 * p.a.y);
    if (chH >= 5) d.x = 0.0;
    if (chV >= 5) d.y = 0.0;

    vec4 moved = vec4(0.0);
    vec2 uv = in_uv(px - d);
    if (here) {
        // Dentro da camada: a borda decide de onde vem o que caiu fora.
        if (int(p.b.y + 0.5) == 1) {
            uv = fract(uv);
        } else {
            const vec2 half_ = 0.5 * p.texel.xy;
            uv = clamp(uv, half_, 1.0 - half_);
        }
        moved = texture(u_tex0, uv);
    } else if (inside01(uv)) {
        // Saída expandida: só o que o deslocamento trouxe para fora.
        moved = texture(u_tex0, uv);
    }
    o_color = mix(original, moved, clamp(p.b.z, 0.0, 1.0));
}

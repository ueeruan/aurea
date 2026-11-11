#version 450
// =============================================================================
//  Aurea / shaders / effects / texture_overlay.frag
//
//  0.5.2: TEXTURA (TextureOverlayEffect.cpp). Uma folha de textura por cima
//  da camada: procedural (common/texture_lib.glsl, a mesma do texto) ou a
//  imagem de outra camada (u_tex1, ladrilhada). Mistura pelo modo escolhido,
//  com relevo pela variação da altura da textura e, se pedido, também fora do
//  alfa da camada (a folha cobre a caixa inteira).
//
//  Uniforms (AeUniforms, ae_common.glsl):
//    p0: x = gerador (0 = imagem da camada), y = modo (0 sobrepor, 1 multiplicar,
//        2 tela, 3 luz suave, 4 normal, 5 adicionar), z = opacidade, w = unidade
//        de textura (px da camada)
//    p1: x = rotação (rad), y = fase da animação, z = 1 só dentro da camada,
//        w = relevo (0..1)
//    p2: x = contraste (−1..1), y = semente, z = 1 há camada, w = 1 vidro fosco
//    p3: região da imagem da outra camada (px da composição)
// =============================================================================
#include "ae_common.glsl"
#include "../common/texture_lib.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;

vec3 soft_light(vec3 b, vec3 t) {
    return mix(2.0 * b * t + b * b * (1.0 - 2.0 * t), sqrt(max(b, vec3(0.0))) * (2.0 * t - 1.0) + 2.0 * b * (1.0 - t),
               step(0.5, t));
}

vec3 blend(vec3 b, vec3 t, int mode) {
    if (mode == 1) return b * t;
    if (mode == 2) return 1.0 - (1.0 - b) * (1.0 - t);
    if (mode == 3) return soft_light(b, t);
    if (mode == 4) return t;
    if (mode == 5) return min(b + t, vec3(1.0));
    return mix(2.0 * b * t, 1.0 - 2.0 * (1.0 - b) * (1.0 - t), step(0.5, b));   // sobrepor
}

void main() {
    const vec2 px = ae_px();
    const vec2 half_ = ae_layer_size() * 0.5;
    const float unit = max(u.p[0].w, 1.0);
    const float c = cos(u.p[1].x), s = sin(u.p[1].x);
    const vec2 d = (px - half_) / unit;
    const vec2 q = vec2(c * d.x + s * d.y, -s * d.x + c * d.y);
    const int id = int(u.p[0].x + 0.5);

    vec3 top;
    float h;
    float texAlpha = 1.0;
    if (id == 0) {
        // A imagem da outra camada é o ladrilho, na escala e rotação pedidas.
        const vec2 tpx = q * unit + half_;
        const vec4 t = ae_decode(texture(u_tex1, tpx / max(u.p[3].zw, vec2(1.0))));
        top = t.rgb;
        h = ae_luma(t.rgb);
        texAlpha = t.a;
    } else {
        const AtxTexel tx = atx_texel(id, q, u.p[1].y, vec3(0.0, 0.0, 1.0), u.p[2].y);
        top = tx.albedo;
        h = tx.height;
    }
    top = clamp((top - 0.5) * (1.0 + u.p[2].x) + 0.5, 0.0, 1.0);
    if (u.p[1].w > 0.0) {
        // Relevo: a variação da altura por unidade de textura, iluminada de
        // cima à esquerda (o lado que sobe para a luz clareia).
        const vec2 g = vec2(dFdx(h), dFdy(h)) * u.texel.z * unit;
        const float shade = clamp(dot(g, vec2(0.7071, 0.7071)) * 0.05 * u.p[1].w, -0.35, 0.35);
        top = clamp(top + shade, 0.0, 1.0);
    }

    vec2 baseUV = ae_in_uv();
    if (u.p[2].w > 0.5) {
        // Vidro fosco: a camada vista através de um vidro irregular.
        const vec2 off = (vec2(atx_noise(q * 22.0, 3.0), atx_noise(q * 22.0 + 7.3, 5.0)) - 0.5) * 2.0 * min(6.0, unit * 0.03);
        baseUV = ae_uv_of_px(px + off * u.p[0].z);
    }
    const vec4 base = ae_decode(texture(u_tex0, baseUV));
    const float opacity = clamp(u.p[0].z * texAlpha, 0.0, 1.0);
    const vec3 mixed = mix(base.rgb, blend(base.rgb, top, int(u.p[0].y + 0.5)), opacity);
    vec4 outColor = ae_encode(mixed, base.a);
    // Fora da camada também: a folha cobre a caixa inteira, por baixo do que há.
    if (u.p[1].z < 0.5) outColor += ae_encode(top, opacity) * (1.0 - base.a);
    o_color = outColor;
}

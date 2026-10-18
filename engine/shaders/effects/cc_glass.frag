#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_glass.frag
//
//  Vidro (o comportamento do CC Glass): o mapa de relevo é a própria camada
//  (luminância, um canal ou o alfa) já suavizada (u_tex1). A normal sai do
//  gradiente do mapa × "Altura"; a imagem é lida deslocada pela normal
//  (refração de "Deslocamento" px) e iluminada — um vidro moldado com a
//  forma da imagem.
//
//  p0 = propriedade, altura (escala do relevo, px), deslocamento (px), —;
//  p3 = região do mapa suavizado (px da camada).
// =============================================================================
#include "cc_common.glsl"
#include "cc_light.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // mapa suavizado

float bump(vec2 px) {
    const vec4 m = texture(u_tex1, (px - p.p3.xy) / max(p.p3.zw, vec2(1e-6)));
    const int k = int(p.p0.x + 0.5);
    if (k == 1) return m.a;
    if (k == 2) return m.r;
    if (k == 3) return m.g;
    if (k == 4) return m.b;
    return aurea_luma(m.rgb);
}

void main() {
    const vec2 px = cc_px();
    const float e = cc_texel();
    const vec2 slope = vec2(bump(px + vec2(e, 0.0)) - bump(px - vec2(e, 0.0)),
                            bump(px + vec2(0.0, e)) - bump(px - vec2(0.0, e))) / (2.0 * e);
    const vec3 n = normalize(vec3(-slope * p.p0.y, 1.0));
    const vec4 c = cc_at(px + n.xy * p.p0.z);
    o_color = vec4(cc_shade(c.rgb, c.a, n), c.a);
}

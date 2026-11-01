#version 450
// =============================================================================
//  Aurea / shaders / effects / mirror.frag
//
//  Espelho: uma reta passa pelo centro, no ângulo pedido. O lado de "trás" da
//  reta fica como está; o lado da frente é substituído pelo reflexo dele. É
//  uma reflexão exata (sem esticar nem reamostrar a metade que fica).
//
//  O que o reflexo buscaria fora da imagem é transparente — nunca a borda
//  esticada.
//
//  0.4.5 (o Mirror do Alight Motion): "dois eixos" também dobra na reta
//  perpendicular (um quarto se repete nos quatro); "sobrepor cópia" deixa a
//  camada inteira e soma a(s) cópia(s) espelhada(s) por cima ou por trás, com
//  opacidade. p1 = (dois eixos, sobrepor, opacidade da cópia, cópia atrás);
//  zeros e opacidade 1 = o espelho de sempre.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // xy = centro (px da camada), zw = normal da reta (aponta para o lado refletido)
    vec4 p1;   // x = dois eixos, y = sobrepor cópia, z = opacidade da cópia, w = cópia atrás
    vec4 p2;   // região da imagem (px da camada)
    vec4 p3;
    vec4 color;
} p;

/// A imagem no ponto (px da camada); fora dela, transparente.
vec4 at_point(vec2 point) {
    const vec2 uv = (point - p.p2.xy) / max(p.p2.zw, vec2(1e-3));
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return vec4(0.0);
    return texture(u_tex0, uv);
}

void main() {
    const vec2 here = p.p2.xy + v_uv * p.p2.zw;
    const vec2 n = p.p0.zw;
    const vec2 m = vec2(-n.y, n.x);   // a reta perpendicular (dois eixos)
    const bool both = p.p1.x > 0.5;
    if (p.p1.y < 0.5) {
        // Substituir o lado: o lado da frente vira o reflexo do de trás.
        vec2 point = here;
        const float d = dot(point - p.p0.xy, n);
        if (d > 0.0) point -= 2.0 * d * n;
        if (both) {
            const float e = dot(point - p.p0.xy, m);
            if (e > 0.0) point -= 2.0 * e * m;
        }
        o_color = at_point(point);
        return;
    }
    // Sobrepor cópia: a camada inteira + a cópia refletida (com dois eixos, as
    // três cópias: por uma reta, pela outra e pelas duas).
    const vec4 layer = at_point(here);
    const vec2 dn = 2.0 * dot(here - p.p0.xy, n) * n;
    const vec2 dm = 2.0 * dot(here - p.p0.xy, m) * m;
    vec4 copy = at_point(here - dn);
    if (both) {
        const vec4 c2 = at_point(here - dm);
        const vec4 c3 = at_point(here - dn - dm);
        copy = copy + c2 * (1.0 - copy.a);
        copy = copy + c3 * (1.0 - copy.a);
    }
    copy *= clamp(p.p1.z, 0.0, 1.0);
    o_color = p.p1.w > 0.5 ? layer + copy * (1.0 - layer.a) : copy + layer * (1.0 - copy.a);
}

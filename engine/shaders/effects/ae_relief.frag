#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_relief.frag
//
//  Lote "AE 2026", relevo iluminado. u.p[11].w escolhe:
//    0 Texturizar — o relevo de uma textura (u_tex1: outra camada, no quadro
//      da composição; sem camada, uma tela procedural) iluminado por uma luz
//      direcional sobre a camada. p0: x = direção da luz (rad), y = contraste,
//      z = posição (0 ladrilhar, 1 centralizar, 2 esticar), w = 1 há camada;
//      p2 = região da textura (px da composição).
//    1 Plástico — a camada (ou outra) vira uma superfície de altura (u_tex1
//      já desfocado, região p2) com luz difusa e especular. p0: x = propriedade
//      (0 luminância, 1 alfa, 2 R, 3 G, 4 B), y = passo do gradiente (px),
//      z = altura, w = 1 relevo de outra camada; p1: corte mín./máx.,
//      intensidade, tipo (0 distante, 1 pontual); p3: altura e direção da luz
//      (rad), ambiente, difusa; p4: especular, aspereza, metal; c0 = cor da luz.
// =============================================================================
#include "ae_common.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;

// --- Texturizar ---------------------------------------------------------------------
float tex_height(vec2 px) {
    if (u.p[0].w < 0.5) {
        // Tela procedural: trama fina de lona.
        const float weave = 0.5 + 0.25 * sin(px.x * 1.7) * sin(px.y * 1.7);
        return weave * 0.5 + aurea_fbm(px * 0.35, 3, 0.55) * 0.5;
    }
    const vec2 ls = ae_layer_size();
    const vec2 ts = max(u.p[2].zw, vec2(1.0));
    const int placement = int(u.p[0].z + 0.5);
    vec2 uv;
    if (placement == 0) uv = fract(px / ts);
    else if (placement == 1) {
        uv = (px - ls * 0.5 + ts * 0.5) / ts;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return 0.5;
    } else uv = px / ls;
    const vec4 t = ae_decode(texture(u_tex1, uv));
    return mix(0.5, ae_luma(t.rgb), t.a);
}
vec4 texturize(vec4 src) {
    if (src.a <= 0.0) return src;
    const vec2 px = ae_px();
    const float h = max(ae_texel_px(), 1.0);
    const float gx = tex_height(px + vec2(h, 0.0)) - tex_height(px - vec2(h, 0.0));
    const float gy = tex_height(px + vec2(0.0, h)) - tex_height(px - vec2(0.0, h));
    const vec2 light = vec2(cos(u.p[0].x), -sin(u.p[0].x));
    const float shade = -dot(vec2(gx, gy), light) * u.p[0].y * 2.0;
    const vec4 d = ae_decode(src);
    const vec3 c = shade >= 0.0 ? d.rgb + (1.0 - d.rgb) * shade : d.rgb * (1.0 + shade);
    return ae_encode(c, d.a);
}

// --- Plástico -------------------------------------------------------------------------
float bump_height(vec2 px) {
    vec2 at = px;
    if (u.p[0].w > 0.5) at = px / ae_layer_size() * u.p[2].zw + u.p[2].xy;   // outra camada esticada na caixa
    const vec4 b = texture(u_tex1, (at - u.p[2].xy) / max(u.p[2].zw, vec2(1e-3)));
    const vec4 d = ae_decode(b);
    const int prop = int(u.p[0].x + 0.5);
    float h = ae_luma(d.rgb) * d.a;
    if (prop == 1) h = d.a;
    else if (prop == 2) h = d.r * d.a;
    else if (prop == 3) h = d.g * d.a;
    else if (prop == 4) h = d.b * d.a;
    const float lo = u.p[1].x, hi = max(u.p[1].y, lo + 1e-3);
    return clamp((h - lo) / (hi - lo), 0.0, 1.0);
}
vec4 plastic(vec4 src) {
    if (src.a <= 0.0) return src;
    const vec2 px = ae_px();
    const float st = max(u.p[0].y, ae_texel_px());
    const float gx = (bump_height(px + vec2(st, 0.0)) - bump_height(px - vec2(st, 0.0))) / (2.0 * st);
    const float gy = (bump_height(px + vec2(0.0, st)) - bump_height(px - vec2(0.0, st))) / (2.0 * st);
    const float H = u.p[0].z * 20.0;
    const vec3 n = normalize(vec3(-gx * H, -gy * H, 1.0));
    const float elev = u.p[3].x, dir = u.p[3].y;
    vec3 L = vec3(cos(dir) * cos(elev), -sin(dir) * cos(elev), sin(elev));
    if (u.p[1].w > 0.5) {
        // Pontual: a luz fica na direção dada, a meia camada do centro, na altura dada.
        const vec2 ls = ae_layer_size();
        const vec2 lp = ls * 0.5 + vec2(cos(dir), -sin(dir)) * 0.5 * max(ls.x, ls.y);
        const float lh = tan(clamp(elev, 0.05, 1.5)) * 0.5 * max(ls.x, ls.y);
        L = normalize(vec3(lp - px, lh));
    }
    const vec3 V = vec3(0.0, 0.0, 1.0);
    const vec3 Hh = normalize(L + V);
    const float ndl = max(dot(n, L), 0.0);
    const float shin = mix(256.0, 4.0, clamp(u.p[4].y, 0.0, 1.0));
    const float spec = pow(max(dot(n, Hh), 0.0), shin);
    const vec4 d = ae_decode(src);
    const vec3 base = aurea_srgb_to_linear(d.rgb);
    const vec3 lc = max(u.c[0].rgb, vec3(0.0)) * u.p[1].z;
    const vec3 specColor = mix(lc, base * lc, clamp(u.p[4].z, 0.0, 1.0));
    const vec3 lin = base * (u.p[3].z + u.p[3].w * ndl * lc) + spec * u.p[4].x * specColor;
    return vec4(max(lin, vec3(0.0)) * d.a, d.a);
}

void main() {
    const vec4 src = texture(u_tex0, ae_in_uv());
    if (u.p[11].w < 0.5) o_color = texturize(src);
    else o_color = plastic(src);
}

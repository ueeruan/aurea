#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_two_layer.frag
//
//  Lote "AE 2026": os efeitos que comparam a camada com OUTRA (u_tex1, a outra
//  camada desenhada no quadro da composição). u.p[0].x escolhe:
//    0 Mesclar (y = modo, z = misturar com o original)
//    1 Fosco por diferença (y = visualização, z = tolerância, w = suavidade;
//      p[1].x = desfoque antes da diferença, px)
//    2 Paleta tonal guiada pela imagem ou pela máscara opcional
//  u.p[10]: x = 1 há outra camada (0 = a própria entrada), y = 0 centralizar /
//  1 esticar na caixa da camada, zw = tamanho da outra camada (px).
// =============================================================================
#include "ae_common.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;

vec4 other_at(vec2 px) {
    if (u.p[10].x < 0.5) return ae_at_px(px);
    const vec2 ls = ae_layer_size();
    const vec2 os = max(u.p[10].zw, vec2(1.0));
    const vec2 muv = u.p[10].y > 0.5 ? px / ls : (px - ls * 0.5 + os * 0.5) / os;
    return texture(u_tex1, muv);
}

/// Média em cruz de 9 amostras num raio (px da camada): o "desfocar antes".
vec4 soft_self(vec2 px, float r) {
    if (r <= 0.0) return ae_at_px(px);
    vec4 s = ae_at_px(px) * 2.0;
    for (int k = 0; k < 8; ++k) {
        const float a = float(k) * AUREA_TAU / 8.0;
        s += ae_at_px(px + vec2(cos(a), sin(a)) * r);
    }
    return s / 10.0;
}
vec4 soft_other(vec2 px, float r) {
    if (r <= 0.0) return other_at(px);
    vec4 s = other_at(px) * 2.0;
    for (int k = 0; k < 8; ++k) {
        const float a = float(k) * AUREA_TAU / 8.0;
        s += other_at(px + vec2(cos(a), sin(a)) * r);
    }
    return s / 10.0;
}

vec4 blend_layers(vec4 src, vec4 oth) {
    const int mode = int(u.p[0].y + 0.5);
    const float keep = clamp(u.p[0].z, 0.0, 1.0);
    vec4 r;
    if (mode == 0) {
        r = oth;   // transição cruzada: a outra camada inteira (cor e alfa)
    } else {
        const vec4 a = ae_decode(src), b = ae_decode(oth);
        vec3 c = a.rgb;
        const vec3 ha = ae_rgb2hsl(a.rgb), hb = ae_rgb2hsl(b.rgb);
        if (mode == 1) c = ae_hsl2rgb(vec3(hb.x, hb.y, ha.z));                  // só a cor
        else if (mode == 2) c = ae_hsl2rgb(vec3(hb.x, ha.y, ha.z));             // só o tom (matiz)
        else if (mode == 3) c = min(a.rgb, b.rgb);                               // só escurecer
        else c = max(a.rgb, b.rgb);                                              // só clarear
        c = mix(a.rgb, c, b.a);   // onde a outra é transparente, nada muda
        r = ae_encode(c, a.a);
    }
    return mix(r, src, keep);
}

vec4 difference_matte(vec4 src, vec2 px) {
    const float r = u.p[1].x;
    const vec4 a = ae_decode(soft_self(px, r)), b = ae_decode(soft_other(px, r));
    const float diff = max(length(a.rgb - b.rgb) / 1.7320508, abs(a.a - b.a));
    const float tol = u.p[0].z;
    const float m = smoothstep(tol, tol + max(u.p[0].w, 1e-4), diff);   // 1 = diferente (fica)
    const int view = int(u.p[0].y + 0.5);
    if (view == 1) return src;
    if (view == 2) return vec4(vec3(aurea_srgb_to_linear(m)) * src.a, src.a);
    return src * m;
}

float tonal_matte(vec2 px) {
    if (u.p[9].x < 0.5) return 1.0;
    const vec3 point = vec3(px, 1.0);
    const float w = dot(u.p[6].xyz, point);
    if (w <= 1e-6) return 0.0;
    const vec2 comp = vec2(dot(u.p[4].xyz, point), dot(u.p[5].xyz, point)) / w;
    const vec4 matte = texture(u_tex1, (comp - u.p[7].xy) / u.p[7].zw);
    const float weight = u.p[9].y < 0.5 ? aurea_luma(matte.rgb) : matte.a;
    return clamp(u.p[9].z > 0.5 ? 1.0 - weight : weight, 0.0, 1.0);
}

float tonal_segment(float brightness, float low, float high, float softness) {
    float t = clamp((brightness - low) / (high - low), 0.0, 1.0);
    // Each segment still hits both control points after changing its slope.
    const float slope = 1.0 / clamp(softness, 0.001, 16.0);
    return t < 0.5 ? 0.5 * pow(t * 2.0, slope) : 1.0 - 0.5 * pow((1.0 - t) * 2.0, slope);
}

vec4 tonal_palette(vec4 src, vec2 px) {
    const float amount = u.p[0].z * tonal_matte(px);
    if (amount <= 0.0) return src;
    const vec4 decoded = ae_decode(src);
    // Weighting recovered from the reference frames in encoded space.
    const float brightness = dot(decoded.rgb, vec3(0.3125, 0.5, 0.1875));
    const vec3 a = aurea_linear_to_srgb(u.c[0].rgb);
    const vec3 b = aurea_linear_to_srgb(u.c[1].rgb);
    vec3 result;
    if (u.p[0].y < 0.5) {
        const float t = clamp(0.5 + (brightness - u.p[1].x) / u.p[1].y, 0.0, 1.0);
        result = mix(a, b, u.p[1].z > 0.5 ? 1.0 - t : t);
    } else if (brightness < u.p[1].y) {
        result = mix(a, b, tonal_segment(brightness, u.p[1].x, u.p[1].y, u.p[2].x));
    } else {
        const vec3 c = aurea_linear_to_srgb(u.c[2].rgb);
        if (brightness < u.p[1].z) {
            result = mix(b, c, tonal_segment(brightness, u.p[1].y, u.p[1].z, u.p[2].y));
        } else {
            const vec3 d = aurea_linear_to_srgb(u.p[8].rgb);
            result = mix(c, d, tonal_segment(brightness, u.p[1].z, u.p[1].w, u.p[2].z));
        }
    }
    return mix(src, ae_encode(result, src.a), amount);
}

void main() {
    const vec4 src = texture(u_tex0, ae_in_uv());
    const vec2 px = ae_px();
    if (u.p[0].x < 0.5) o_color = blend_layers(src, other_at(px));
    else if (u.p[0].x < 1.5) o_color = difference_matte(src, px);
    else o_color = tonal_palette(src, px);
}

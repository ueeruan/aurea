#version 450
// =============================================================================
//  Aurea / shaders / composite / layer.frag
//
//  Amostra a textura da layer e aplica a opacidade. O blend `Normal`
//  (pré-multiplicado: src + dst*(1-srcA)) é do hardware — o shader não lê o
//  destino, e por isso não há custo de leitura de framebuffer por layer.
// =============================================================================
#include "../common/bindings.glsl"
#include "../scene3d/common/hdr.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;
    vec4 uvRect;
    vec4 params;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

// AE layers: amostragem BICÚBICA (Catmull-Rom em 9 leituras bilineares). A
// imagem é pré-multiplicada: o anel do filtro é preso em [0, alfa].
vec4 sample_bicubic(vec2 uv) {
    const vec2 size = vec2(textureSize(u_tex0, 0));
    const vec2 pos = uv * size;
    const vec2 p1 = floor(pos - 0.5) + 0.5;
    const vec2 f = pos - p1;
    const vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    const vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    const vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    const vec2 w3 = f * f * (-0.5 + 0.5 * f);
    const vec2 w12 = w1 + w2;
    const vec2 t0 = (p1 - 1.0) / size;
    const vec2 t3 = (p1 + 2.0) / size;
    const vec2 t12 = (p1 + w2 / w12) / size;
    vec4 r = texture(u_tex0, vec2(t0.x, t0.y)) * w0.x * w0.y
           + texture(u_tex0, vec2(t12.x, t0.y)) * w12.x * w0.y
           + texture(u_tex0, vec2(t3.x, t0.y)) * w3.x * w0.y
           + texture(u_tex0, vec2(t0.x, t12.y)) * w0.x * w12.y
           + texture(u_tex0, vec2(t12.x, t12.y)) * w12.x * w12.y
           + texture(u_tex0, vec2(t3.x, t12.y)) * w3.x * w12.y
           + texture(u_tex0, vec2(t0.x, t3.y)) * w0.x * w3.y
           + texture(u_tex0, vec2(t12.x, t3.y)) * w12.x * w3.y
           + texture(u_tex0, vec2(t3.x, t3.y)) * w3.x * w3.y;
    r.a = clamp(r.a, 0.0, 1.0);
    r.rgb = clamp(r.rgb, vec3(0.0), vec3(max(r.a, 0.0)) * 65504.0);
    return r;
}

void main() {
    // params.yzw = (2, 2, 2): amostra bicúbica da imagem inteira (não é a
    // máscara de canal do RGB no tempo, que só usa 0/1).
    if (pc.params.y > 1.5) {
        o_color = sample_bicubic(v_uv) * pc.params.x;
        return;
    }
    const vec4 c = texture(u_tex0, v_uv);
    // Exposure integrates decoded scene radiance, not the MSAA resolve
    // encoding. Re-encode only once for the existing bloom/tonemap pipeline.
    if (pc.params.w < -0.5) {
        o_color = vec4(pc.params.w < -1.5 ? aurea_hdr_encode(c.rgb) : aurea_hdr_decode(c.rgb), c.a) * pc.params.x;
        return;
    }
    // params.yzw ≠ 0: amostra de UM canal (RGB no tempo) — o canal inteiro e
    // um terço do alfa (três amostras somadas refazem a cor e o alfa).
    const vec3 mask = pc.params.yzw;
    if (mask.x + mask.y + mask.z > 0.0) {
        o_color = vec4(c.rgb * mask, c.a / 3.0) * pc.params.x;
    } else {
        o_color = c * pc.params.x;
    }
}

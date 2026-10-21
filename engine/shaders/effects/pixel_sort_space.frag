#version 450
// =============================================================================
//  Aurea / shaders / effects / pixel_sort_space.frag
//
//  Ordenar pixels (0.3.0), a ida e a volta do ESPAÇO DE ORDENAÇÃO: a imagem é
//  levada a uma grade em que a direção pedida vira as linhas (ângulo livre,
//  diagonal, radial em coordenadas polares, ou o passo em blocos), o
//  pixel_sort.comp ordena as linhas, e o caminho de volta traz o resultado à
//  camada. Contas em px da CAMADA: o mesmo desenho em qualquer resolução.
//
//   modo 0 = ida girada   (alvo = grade; px = centro + R(θ)·(texel − grade/2)/densidade)
//   modo 1 = volta girada (alvo = camada; texel = R(−θ)·(px − centro)·densidade + grade/2)
//   modo 2 = ida polar    (x = raio, y = ângulo)
//   modo 3 = volta polar
//  Na volta, onde a ordenação não mexeu (a grade ordenada = a grade de ida) o
//  pixel ORIGINAL fica — sem a dupla reamostragem nas áreas paradas.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // ida: a camada; volta: a grade ordenada
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // volta: a grade de ida (antes de ordenar)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_tex2;   // volta: a camada original

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 a0;   // x = modo, y = cos θ, z = sen θ, w = densidade da grade (texels por px da camada)
    vec4 a1;   // xy = centro (px da camada), zw = tamanho da grade (texels)
    vec4 a2;   // região da camada (px): x, y, largura, altura
    vec4 a3;   // x = linhas de ângulo da grade polar
} p;

const float kTwoPi = 6.28318530718;

vec4 layer_at(vec2 px) {
    const vec2 uv = (px - p.a2.xy) / max(p.a2.zw, vec2(1e-6));
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);
    return texture(u_tex0, uv);
}

void main() {
    const int mode = int(p.a0.x + 0.5);
    const float density = max(p.a0.w, 1e-6);
    const vec2 grid = max(p.a1.zw, vec2(1.0));
    if (mode == 0 || mode == 2) {
        const vec2 texel = v_uv * grid;
        vec2 px;
        if (mode == 0) {
            const vec2 q = (texel - 0.5 * grid) / density;
            px = p.a1.xy + vec2(p.a0.y * q.x - p.a0.z * q.y, p.a0.z * q.x + p.a0.y * q.y);
        } else {
            const float r = texel.x / density;
            const float a = texel.y / max(p.a3.x, 1.0) * kTwoPi;
            px = p.a1.xy + r * vec2(cos(a), sin(a));
        }
        o_color = layer_at(px);
        return;
    }
    const vec2 px = p.a2.xy + v_uv * p.a2.zw;
    const vec2 d = px - p.a1.xy;
    vec2 texel;
    if (mode == 1) {
        texel = vec2(p.a0.y * d.x + p.a0.z * d.y, -p.a0.z * d.x + p.a0.y * d.y) * density + 0.5 * grid;
    } else {
        float a = atan(d.y, d.x);
        if (a < 0.0) a += kTwoPi;
        texel = vec2(length(d) * density, a / kTwoPi * max(p.a3.x, 1.0));
    }
    const vec2 uv = texel / grid;
    const vec4 sorted = texture(u_tex0, uv);
    const vec4 before = texture(u_tex1, uv);
    const vec4 original = texture(u_tex2, v_uv);
    const vec4 diff = abs(sorted - before);
    o_color = max(max(diff.r, diff.g), max(diff.b, diff.a)) < 1e-4 ? original : sorted;
}

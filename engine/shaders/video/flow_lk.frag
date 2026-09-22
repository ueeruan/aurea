#version 450
// =============================================================================
//  Aurea / shaders / video / flow_lk.frag
//
//  Um nível da pirâmide do optical flow (Lucas-Kanade iterativo): parte do
//  fluxo do nível mais grosso (×2) e refina com janela 5×5. O gradiente do
//  quadro atual é o mesmo em todas as iterações (medido uma vez); só o quadro
//  seguinte é reamostrado no deslocamento corrente.
//
//  Saída: RG = fluxo em pixels DESTE nível (atual → seguinte), B = confiança
//  (menor autovalor da matriz de estrutura; 0 = região lisa, sem informação).
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_level;    // R = atual, G = seguinte
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_coarse;   // RG = fluxo do nível de cima (px dele)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;    // xy = 1/tamanho do nível, zw = tamanho
    vec4 flags;    // x = tem nível de cima (0/1), y = iterações
} p;

void main() {
    vec2 flow = p.flags.x > 0.5 ? texture(u_coarse, v_uv).xy * 2.0 : vec2(0.0);
    float ix[25], iy[25], av[25];
    float gxx = 0.0, gxy = 0.0, gyy = 0.0;
    for (int k = 0; k < 25; ++k) {
        vec2 uv = v_uv + vec2(float(k % 5 - 2), float(k / 5 - 2)) * p.texel.xy;
        float gx = 0.5 * (texture(u_level, uv + vec2(p.texel.x, 0.0)).r - texture(u_level, uv - vec2(p.texel.x, 0.0)).r);
        float gy = 0.5 * (texture(u_level, uv + vec2(0.0, p.texel.y)).r - texture(u_level, uv - vec2(0.0, p.texel.y)).r);
        ix[k] = gx;
        iy[k] = gy;
        av[k] = texture(u_level, uv).r;
        gxx += gx * gx;
        gxy += gx * gy;
        gyy += gy * gy;
    }
    float det = gxx * gyy - gxy * gxy;
    // Menor autovalor: confiança do rastreio (canto > borda > liso).
    float tr = gxx + gyy;
    float lmin = 0.5 * (tr - sqrt(max(tr * tr - 4.0 * det, 0.0)));
    if (det > 1e-9) {
        int iters = int(p.flags.y);
        for (int it = 0; it < 8; ++it) {
            if (it >= iters) break;
            float bx = 0.0, by = 0.0;
            for (int k = 0; k < 25; ++k) {
                vec2 uv = v_uv + (vec2(float(k % 5 - 2), float(k / 5 - 2)) + flow) * p.texel.xy;
                float dt = texture(u_level, uv).g - av[k];
                bx += ix[k] * dt;
                by += iy[k] * dt;
            }
            vec2 d = -vec2(gyy * bx - gxy * by, -gxy * bx + gxx * by) / det;
            // Passo limitado: LK é linear só perto da solução.
            float len = length(d);
            if (len > 2.0) d *= 2.0 / len;
            flow += d;
            if (len < 0.01) break;
        }
    }
    o_color = vec4(flow, lmin, 1.0);
}

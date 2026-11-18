#version 450
// =============================================================================
//  Aurea / shaders / video / flow_var.frag
//
//  DIS — refinamento variacional (1.0.3; a etapa final de cada nível do DIS,
//  Kroeger et al. ECCV 2016, sobre a energia de Brox et al. 2004). Uma
//  iteração de Jacobi da equação linearizada em torno do campo denso u₀:
//
//    dados:      ψ((I₁(x+u) − I₀(x))²)            constância de brilho
//              + γ·ψ(|∇I₁(x+u) − ∇I₀(x)|²)         constância do gradiente
//                (luz que muda devagar não vira vetor)
//    suavidade:  α·Σ wₙ·ψ(|u − uₙ|²)              com wₙ pela semelhança de COR
//                (luminância + crominância) entre o pixel e o vizinho — o
//                campo é liso dentro do braço e dobra no contorno dele, mesmo
//                quando só a cor separa o braço do fundo.
//
//  ψ(s²) = √(s² + ε²) (robusta: o erro grande de uma oclusão não puxa o
//  resto). A não linearidade é atrasada (pesos da iteração anterior), como no
//  esquema de pontos fixos do artigo; a matriz 2×2 de cada pixel é resolvida
//  direto.
//
//  Entrada: nível (R = atual, G = seguinte), guia de cor, u₀ (RG, B = conf.)
//  e u da iteração anterior. Saída: RG = u novo (px do nível), B = conf.
// =============================================================================
#include "../common/bindings.glsl"
#include "flow_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_level;   // R = atual, G = seguinte
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_guide;   // RG = CbCr do atual, BA = do seguinte
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_base;    // RG = u₀ (ponto da linearização), B = confiança
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_cur;     // RG = u da iteração anterior

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;    // xy = 1/tamanho do nível, zw = tamanho
    vec4 weights;  // x = α (suavidade), y = γ (gradiente), z = σ da cor nos pesos de vizinho, w = sentido trocado (0/1)
    vec4 eps;      // x = ε dos dados, y = ε da suavidade (px), z = peso da crominância na cor
} p;

float src_l(vec2 uv) { const vec2 c = textureLod(u_level, uv, 0.0).rg; return p.weights.w > 0.5 ? c.g : c.r; }
float dst_l(vec2 uv) { const vec2 c = textureLod(u_level, uv, 0.0).rg; return p.weights.w > 0.5 ? c.r : c.g; }
vec2 src_c(vec2 uv) { const vec4 c = textureLod(u_guide, uv, 0.0); return p.weights.w > 0.5 ? c.ba : c.rg; }

void main() {
    const vec2 tx = vec2(p.texel.x, 0.0), ty = vec2(0.0, p.texel.y);
    const vec4 base = textureLod(u_base, v_uv, 0.0);
    const vec2 u0 = base.rg;
    const vec2 u = textureLod(u_cur, v_uv, 0.0).rg;
    // Quadro de origem: cruz em x (gradiente e cor dos vizinhos).
    const float s = src_l(v_uv);
    const float sxp = src_l(v_uv + tx), sxm = src_l(v_uv - tx), syp = src_l(v_uv + ty), sym = src_l(v_uv - ty);
    // Outro quadro na linearização: 3×3 em x + u₀.
    const vec2 w = v_uv + u0 * p.texel.xy;
    const float d = dst_l(w);
    const float dxp = dst_l(w + tx), dxm = dst_l(w - tx), dyp = dst_l(w + ty), dym = dst_l(w - ty);
    const float dpp = dst_l(w + tx + ty), dpm = dst_l(w + tx - ty), dmp = dst_l(w - tx + ty), dmm = dst_l(w - tx - ty);
    const float i0x = 0.5 * (sxp - sxm), i0y = 0.5 * (syp - sym);
    const float i1x = 0.5 * (dxp - dxm), i1y = 0.5 * (dyp - dym);
    const float ix = 0.5 * (i0x + i1x), iy = 0.5 * (i0y + i1y), it = d - s;
    const float ixx = dxp - 2.0 * d + dxm, iyy = dyp - 2.0 * d + dym, ixy = 0.25 * (dpp - dpm - dmp + dmm);
    const float ixt = i1x - i0x, iyt = i1y - i0y;
    const vec2 du = u - u0;
    // Pesos robustos dos dados no u atual (linearizado).
    const float e2 = p.eps.x * p.eps.x;
    const float rb = ix * du.x + iy * du.y + it;
    const float pb = 0.5 / sqrt(rb * rb + e2);
    const float r1 = ixx * du.x + ixy * du.y + ixt, r2 = ixy * du.x + iyy * du.y + iyt;
    const float pg = p.weights.y * 0.5 / sqrt(r1 * r1 + r2 * r2 + e2);
    const float a11 = pb * ix * ix + pg * (ixx * ixx + ixy * ixy);
    const float a12 = pb * ix * iy + pg * (ixx * ixy + ixy * iyy);
    const float a22 = pb * iy * iy + pg * (ixy * ixy + iyy * iyy);
    const float c1 = pb * it * ix + pg * (ixx * ixt + ixy * iyt);
    const float c2 = pb * it * iy + pg * (ixy * ixt + iyy * iyt);
    // Suavidade: 4 vizinhos, peso pela cor e robusto na diferença do fluxo.
    const vec2 cc = src_c(v_uv);
    const float sigma = max(p.weights.z, 1e-3);
    const float inv2s2 = 0.5 / (sigma * sigma);
    const float es2 = p.eps.y * p.eps.y;
    vec2 nsum = vec2(0.0);
    float wsum = 0.0;
    const vec2 offs[4] = vec2[4](vec2(1.0, 0.0), vec2(-1.0, 0.0), vec2(0.0, 1.0), vec2(0.0, -1.0));
    const float nl[4] = float[4](sxp, sxm, syp, sym);
    for (int k = 0; k < 4; ++k) {
        const vec2 at = v_uv + offs[k] * p.texel.xy;
        const vec2 un = textureLod(u_cur, at, 0.0).rg;
        const float dl = nl[k] - s;
        const vec2 dc = src_c(at) - cc;
        const vec2 df = u - un;
        const float wn = p.weights.x * exp(-flow_color_dist2(dl, dc, p.eps.z) * inv2s2) * 0.5 / sqrt(dot(df, df) + es2);
        nsum += un * wn;
        wsum += wn;
    }
    // (A + Σwₙ) u = A u₀ − c + Σ wₙ uₙ
    const float m11 = a11 + wsum, m12 = a12, m22 = a22 + wsum;
    const vec2 rhs = vec2(a11 * u0.x + a12 * u0.y - c1, a12 * u0.x + a22 * u0.y - c2) + nsum;
    const float det = m11 * m22 - m12 * m12;
    vec2 un = det > 1e-12 ? vec2(m22 * rhs.x - m12 * rhs.y, -m12 * rhs.x + m11 * rhs.y) / det : u;
    // Passo limitado: a linearização só vale perto de u₀.
    const vec2 step = un - u0;
    const float len = length(step);
    if (len > 1.5) un = u0 + step * (1.5 / len);
    o_color = vec4(un, base.b, 1.0);
}

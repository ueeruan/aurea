#version 450
// =============================================================================
//  Aurea / shaders / effects / enhance_deblock.frag
//
//  Aprimorar, "Reverter compressão": tira os BLOCOS do codec (DCT 8×8; as
//  bordas de macrobloco 16×16 caem na mesma grade). Um eixo por passe
//  (separável: horizontal e depois vertical). A grade é a da FONTE, em px da
//  camada — no preview reduzido ela continua no lugar.
//
//  Na borda B entre p0 e q0 (amostras p3 p2 p1 p0 | q0 q1 q2 q3):
//      degrau do codec  a = (q0 − p0) − ½·[(p0 − p1) + (q1 − q0)]
//  (o degrau menos o que a inclinação dos dois lados já explica — um degradê
//  de verdade não é bloco). Ele é desfeito numa rampa de 4 px de cada lado,
//  só quando é PEQUENO (borda real passa do limiar α) e os blocos são LISOS
//  por dentro (atividade < β): é onde o bloco aparece, e é a mesma decisão do
//  filtro de desbloqueio dos codecs. α e β crescem com a força e com o ruído.
// =============================================================================
#include "../common/bindings.glsl"
#include "enhance_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_w;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho, zw = tamanho (texels)
    vec4 grid;    // x = texels por px da camada (neste eixo), y = origem da região (px), z = eixo (0 x, 1 y), w = entrada linear
} p;

void main() {
    const bool lin = p.grid.w > 0.5;
    const vec4 c = enh_load(u_src, v_uv, lin);
    const float rc = enh_w0(u_w).x;
    if (rc <= 0.0) { o_color = c; return; }
    const float sigma = enh_w1(u_w).z;
    const bool axisX = p.grid.z < 0.5;
    const float s = max(p.grid.x, 1e-3);
    const float size = axisX ? p.texel.z : p.texel.w;
    const float t = (axisX ? v_uv.x : v_uv.y) * size;     // texels (o centro é k + ½)
    const float lx = p.grid.y + t / s;                       // px da camada
    const float bnd = floor(lx / 8.0 + 0.5) * 8.0;
    const float d = lx - bnd;
    if (abs(d) >= 4.0) { o_color = c; return; }
    vec3 v[8];
    for (int k = 0; k < 8; ++k) {
        const float u = ((bnd + float(k - 4) + 0.5) - p.grid.y) * s / size;
        v[k] = enh_load(u_src, axisX ? vec2(u, v_uv.y) : vec2(v_uv.x, u), lin).xyz;
    }
    const vec3 p0 = v[3], p1 = v[2], q0 = v[4], q1 = v[5];
    const vec3 a = (q0 - p0) - 0.5 * ((p0 - p1) + (q1 - q0));
    const float actP = (abs(v[3].x - v[2].x) + abs(v[2].x - v[1].x) + abs(v[1].x - v[0].x)) / 3.0;
    const float actQ = (abs(v[4].x - v[5].x) + abs(v[5].x - v[6].x) + abs(v[6].x - v[7].x)) / 3.0;
    const float alpha = 0.012 + 0.10 * rc + 2.0 * sigma;
    const float beta = 0.006 + 0.03 * rc + 1.5 * sigma;
    const float g = (1.0 - smoothstep(0.6 * alpha, alpha, abs(a.x)))
                  * (1.0 - smoothstep(0.5 * beta, beta, max(actP, actQ)));
    const float f = clamp(1.0 - abs(d) / 4.0, 0.0, 1.0);
    const float side = d < 0.0 ? 1.0 : -1.0;
    o_color = vec4(c.xyz + side * 0.5 * a * (f * g * min(1.0, 2.0 * rc)), c.w);
}

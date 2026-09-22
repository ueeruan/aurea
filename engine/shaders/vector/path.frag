#version 450
// =============================================================================
//  Aurea / shaders / vector / path.frag
//
//  Cobertura exata da borda: a distância assinada interpolada na franja,
//  dividida pelo tamanho do texel na tela (gradiente da distância) — nítido
//  em qualquer escala, sem MSAA. Tinta sólida ou degradê (linear/radial, até
//  8 paradas) entre cores lineares pré-multiplicadas. Saída pré-multiplicada.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_DATA, std430) readonly buffer Data { vec4 d[]; } data;

layout(location = 0) in vec2 v_grad;
layout(location = 1) in float v_dist;
layout(location = 2) flat in int v_paint;
layout(location = 3) flat in float v_alpha;
layout(location = 0) out vec4 o_color;

void main() {
    const float px = max(length(vec2(dFdx(v_dist), dFdy(v_dist))), 1e-6);
    const float cov = clamp(0.5 + v_dist / px, 0.0, 1.0);
    const int p = v_paint;
    const vec4 hdr = data.d[p];   // tipo, nº de paradas, opacidade
    vec4 col;
    if (hdr.x < 0.5) {
        col = data.d[p + 2];
    } else {
        const vec4 pts = data.d[p + 1];
        float t;
        if (hdr.x < 1.5) {
            const vec2 dir = pts.zw - pts.xy;
            t = dot(v_grad - pts.xy, dir) / max(dot(dir, dir), 1e-8);
        } else {
            t = length(v_grad - pts.xy) / max(length(pts.zw - pts.xy), 1e-6);
        }
        const int n = clamp(int(hdr.y + 0.5), 1, 8);
        const vec4 o0 = data.d[p + 10], o1 = data.d[p + 11];
        const float pos[8] = float[8](o0.x, o0.y, o0.z, o0.w, o1.x, o1.y, o1.z, o1.w);
        col = data.d[p + 2];
        if (t >= pos[n - 1]) {
            col = data.d[p + 2 + n - 1];
        } else if (t > pos[0]) {
            for (int i = 1; i < 8; ++i) {
                if (i >= n) break;
                if (t <= pos[i]) {
                    const float span = max(pos[i] - pos[i - 1], 1e-6);
                    col = mix(data.d[p + 2 + i - 1], data.d[p + 2 + i], clamp((t - pos[i - 1]) / span, 0.0, 1.0));
                    break;
                }
            }
        }
    }
    o_color = col * (cov * hdr.z * v_alpha);
}

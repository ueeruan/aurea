#version 450
// Ampliação com preservação de detalhes (referência do AE): o passa-baixa
// L0 da escala de entrada (gaussiano separável, sigma medido 0,54 px).
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_source;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    ivec4 size;    // largura, altura, eixo (0 = x), raio
    vec4 sigma;    // sigma
} p;

void main() {
    ivec2 at = clamp(ivec2(floor(v_uv * vec2(p.size.xy))), ivec2(0), p.size.xy - 1);
    ivec2 dir = p.size.z == 0 ? ivec2(1, 0) : ivec2(0, 1);
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int i = -p.size.w; i <= p.size.w; ++i) {
        float w = exp(-float(i * i) / (2.0 * p.sigma.x * p.sigma.x));
        sum += w * texelFetch(u_source, clamp(at + dir * i, ivec2(0), p.size.xy - 1), 0);
        total += w;
    }
    o_color = sum / total;
}

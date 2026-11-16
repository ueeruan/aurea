#version 450
// Ampliação com preservação de detalhes (referência do AE): para cada centro
// da grade de remendos 5x5 na imagem ampliada L1, procura entre o ponto
// correspondente da entrada e os 4 vizinhos o remendo mais parecido do
// passa-baixa L0 (soma dos quadrados nos 3 canais). O vizinho só vale se a
// distância dele for menos da metade da do ponto correspondente.
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_up;    // L1
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_low;   // L0
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    ivec4 upSize;   // largura, altura de L1; passo da grade; primeiro centro
    ivec4 srcSize;  // largura, altura da entrada; último centro x, y
} p;

vec3 up(ivec2 at) { return texelFetch(u_up, clamp(at, ivec2(0), p.upSize.xy - 1), 0).rgb; }
vec3 low(ivec2 at) { return texelFetch(u_low, clamp(at, ivec2(0), p.srcSize.xy - 1), 0).rgb; }

float distance2(ivec2 c, ivec2 s) {
    float sum = 0.0;
    for (int j = -2; j <= 2; ++j)
        for (int i = -2; i <= 2; ++i) {
            vec3 d = up(c + ivec2(i, j)) - low(s + ivec2(i, j));
            sum += dot(d, d);
        }
    return sum;
}

ivec2 candidate(int k) {
    if (k == 1) return ivec2(0, 1);
    if (k == 2) return ivec2(1, 0);
    if (k == 3) return ivec2(0, -1);
    if (k == 4) return ivec2(-1, 0);
    return ivec2(0, 0);
}

void main() {
    ivec2 c = clamp(ivec2(floor(v_uv * vec2(p.upSize.xy))), ivec2(0), p.upSize.xy - 1);
    int stride = p.upSize.z;
    ivec2 rel = c - ivec2(p.upSize.w);
    if (rel.x < 0 || rel.y < 0 || c.x > p.srcSize.z || c.y > p.srcSize.w || rel.x % stride != 0 || rel.y % stride != 0) {
        o_color = vec4(0.0);
        return;
    }
    ivec2 s = ivec2(floor((vec2(c) - 0.5) * (2.0 / 3.0)));
    float d0 = 0.0;
    float best = 3.0e38;
    ivec2 pick = ivec2(0);
    for (int k = 0; k < 5; ++k) {
        float d = distance2(c, s + candidate(k));
        if (k == 0) d0 = d;
        if (d < best) { best = d; pick = candidate(k); }
    }
    if (d0 > 0.0 && best / d0 >= 0.5) pick = ivec2(0);
    o_color = vec4(vec2(pick), 1.0, 0.0);
}

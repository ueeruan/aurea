#version 450
// Ampliação com preservação de detalhes (referência do AE): L1 + a média, nos
// remendos 5x5 que cobrem o pixel, do detalhe (entrada - L0) copiado do
// remendo escolhido. A borda de 16-17 px fica só com L1, como no nativo.
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_up;      // L1
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_source;  // entrada
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_low;     // L0
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_match;   // escolha por centro
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    ivec4 upSize;   // largura, altura de L1; passo da grade; primeiro centro
    ivec4 srcSize;  // largura, altura da entrada; último centro x, y
    ivec4 inner;    // primeiro pixel com detalhe x/y, fim exclusivo x/y
} p;

vec4 src(ivec2 at) { return texelFetch(u_source, clamp(at, ivec2(0), p.srcSize.xy - 1), 0); }
vec4 low(ivec2 at) { return texelFetch(u_low, clamp(at, ivec2(0), p.srcSize.xy - 1), 0); }

void main() {
    ivec2 x = clamp(ivec2(floor(v_uv * vec2(p.upSize.xy))), ivec2(0), p.upSize.xy - 1);
    vec4 base = texelFetch(u_up, x, 0);
    if (x.x < p.inner.x || x.y < p.inner.y || x.x >= p.inner.z || x.y >= p.inner.w) {
        o_color = base;
        return;
    }
    int stride = p.upSize.z;
    int first = p.upSize.w;
    vec3 sum = vec3(0.0);
    float count = 0.0;
    for (int j = -2; j <= 2; ++j) {
        int cy = x.y + j;
        if (cy < first || cy > p.srcSize.w || (cy - first) % stride != 0) continue;
        for (int i = -2; i <= 2; ++i) {
            int cx = x.x + i;
            if (cx < first || cx > p.srcSize.z || (cx - first) % stride != 0) continue;
            ivec2 c = ivec2(cx, cy);
            vec4 m = texelFetch(u_match, c, 0);
            if (m.z < 0.5) continue;
            ivec2 s = ivec2(floor((vec2(c) - 0.5) * (2.0 / 3.0))) + ivec2(round(m.xy)) + (x - c);
            sum += src(s).rgb - low(s).rgb;
            count += 1.0;
        }
    }
    o_color = count > 0.0 ? vec4(base.rgb + sum / count, base.a) : base;
}

#version 450
// =============================================================================
//  Aurea / shaders / effects / headbop_fill_down.frag
//
//  Headbop, fundo preenchido (push-pull, metade "push"): cada nível guarda a
//  cor do FUNDO ponderada (rgb·peso, peso), com peso 0 onde está a pessoa.
//  Primeiro nível: a camada (4×4 amostras por texel) e a máscara; os
//  seguintes: a média 2×2 do nível de baixo.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // camada / nível anterior
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // máscara (só no primeiro)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;   // uv da camada = uv * xy + zw (primeiro nível)
    vec4 texel;   // xy = 1 / tamanho da SAÍDA
    vec4 p0;      // x = primeiro nível
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

void main() {
    if (p.p0.x > 0.5) {
        vec4 acc = vec4(0.0);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                vec2 o = (vec2(float(x), float(y)) + 0.5) / 4.0 - 0.5;
                vec2 f = v_uv + o * p.texel.xy;   // fração da camada
                vec4 c = texture(u_tex0, f * p.uvMap.xy + p.uvMap.zw);
                float m = smoothstep(0.12, 0.4, texture(u_tex1, clamp(f, vec2(0.0), vec2(1.0))).r);
                if (p.p0.y > 0.5) {
                    // Pirâmide do alfa: todo vizinho fora da pessoa conta, até o
                    // transparente — o buraco herda a transparência em volta.
                    float wa = 1.0 - m;
                    acc += vec4(c.a * wa, 0.0, 0.0, wa);
                    continue;
                }
                float w = (1.0 - m) * step(0.002, c.a);
                acc += vec4(c.rgb / max(c.a, 1e-4) * w, w);
            }
        }
        o_color = acc / 16.0;
        return;
    }
    vec2 h = p.texel.xy * 0.25;
    o_color = 0.25 * (texture(u_tex0, v_uv + vec2(-h.x, -h.y)) + texture(u_tex0, v_uv + vec2(h.x, -h.y))
                    + texture(u_tex0, v_uv + vec2(-h.x, h.y)) + texture(u_tex0, v_uv + vec2(h.x, h.y)));
}

#version 450
// =============================================================================
//  Aurea / shaders / effects / crop_edges.frag
//
//  Cortar bordas: some com o que passa das quatro margens (em px da camada),
//  com uma borda suave opcional PARA DENTRO — o corte nunca mostra mais do que
//  o retângulo pedido. Pré-multiplicado: só se escala a cor.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // retângulo que fica (px da camada): x0, y0, x1, y1
    vec4 p1;   // x = suavidade (px da camada)
    vec4 p2;   // região da imagem (px da camada)
    vec4 p3;
    vec4 color;
} p;

void main() {
    const vec4 source = texture(u_tex0, v_uv);
    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const float inside = min(min(point.x - p.p0.x, p.p0.z - point.x), min(point.y - p.p0.y, p.p0.w - point.y));
    const float feather = max(p.p1.x, 0.0);
    const float keep = feather > 1e-4 ? smoothstep(0.0, feather, inside) : step(0.0, inside);
    o_color = source * keep;
}

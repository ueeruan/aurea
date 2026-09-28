#version 450
// =============================================================================
//  Aurea / shaders / effects / checkerboard.frag
//
//  Xadrez: células alternadas pintadas com a cor do padrão sobre a camada. O
//  tamanho da célula é em pixels da LAYER, então o mesmo número desenha o
//  mesmo xadrez no preview e no export, e continua com o mesmo tamanho quando
//  a camada é escalada.
//
//  A paridade vem do ÍNDICE da célula (`mod(i, 2)`), não de um limiar em uv:
//  assim o padrão continua certo à esquerda e acima da âncora, onde fract()
//  daria o mesmo resultado para duas células vizinhas.
//
//  As bordas são anti-serrilhadas por cobertura (a mesma `aurea_band_coverage`
//  das listras): o w do traço é meio pixel, mais o `feather` que a pessoa
//  pedir. O alfa da camada é preservado — quem era transparente continua.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;   // x = 1/largura, y = 1/altura (px da layer → uv)
    vec4 p0;      // x = largura da célula (uv), y = altura da célula (uv), z = âncora X, w = âncora Y
    vec4 p1;      // x = rotação (rad), y = feather (uv), z = 1 inverte, w = opacidade
    vec4 p2;
    vec4 p3;
    vec4 color;   // cor do padrão
} p;

void main() {
    const vec4 base = texture(u_tex0, v_uv);
    if (base.a <= 1e-5) { o_color = base; return; }

    const vec2 cell = max(p.p0.xy, vec2(1e-6));
    const vec2 c = v_uv - p.p0.zw;
    const float ca = cos(p.p1.x), sa = sin(p.p1.x);
    const vec2 q = vec2(c.x * ca - c.y * sa, c.x * sa + c.y * ca);

    const vec2 g = q / cell;
    const vec2 i = floor(g);
    const vec2 f = (g - i) * cell;                 // onde na célula, em uv
    const vec2 d = min(f, cell - f);               // distância até a borda mais perto, por eixo
    // mod() nunca é negativo em GLSL: a paridade segue válida à esquerda da âncora.
    const vec2 parity = vec2(1.0) - 2.0 * mod(i, vec2(2.0));
    const float w = 0.5 * max(p.texel.x, p.texel.y) + max(p.p1.y, 0.0);
    const vec2 ramp = clamp(d / max(w, 1e-9), 0.0, 1.0) * parity;
    float m = 0.5 + 0.5 * ramp.x * ramp.y;
    m = mix(m, 1.0 - m, step(0.5, p.p1.z));

    const float amount = clamp(m, 0.0, 1.0) * clamp(p.p1.w, 0.0, 1.0) * clamp(p.color.a, 0.0, 1.0);
    // Mistura na cor RETa e volta a pré-multiplicar: o alfa da camada manda.
    const vec3 straight = base.rgb / base.a;
    const vec3 mixed = mix(straight, p.color.rgb, amount);
    o_color = vec4(mixed * base.a, base.a);
}

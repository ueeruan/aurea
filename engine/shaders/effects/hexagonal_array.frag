#version 450
// =============================================================================
//  Aurea / shaders / effects / hexagonal_array.frag
//
//  Matriz hexagonal: a malha de favos desenhada como CONTORNO das células —
//  é o painel de abelha, o telhado de vidro, o padrão de LED hexagonal. O
//  tamanho da célula é a largura de face a face, em pixels da LAYER.
//
//  Duas redes intercaladas meia célula de distância: a mais próxima do
//  fragmento é a célula dele. A distância até a aresta mais perto é o
//  inraio (0.5) menos a maior projeção nas normais das arestas — (1, 0) para
//  os lados verticais, (cos 60, sen 60) para os inclinados; o abs() dobra as
//  outras quatro nessas duas.
//
//  O traço é anti-serrilhado por cobertura, e o alfa da camada é preservado.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;   // x = 1/largura, y = 1/altura (px da layer → uv)
    vec4 p0;      // x = tamanho da célula (uv da ALTURA), y = âncora X, z = âncora Y, w = largura do traço (uv)
    vec4 p1;      // x = rotação (rad), y = feather (uv), z = 1 inverte, w = opacidade
    vec4 p2;
    vec4 p3;
    vec4 color;   // cor do traço
} p;

// Largura de face a face de um hexágono de lado 1 (a altura da malha).
const float kHexHeight = 1.7320508;

void main() {
    const vec4 base = texture(u_tex0, v_uv);
    if (base.a <= 1e-5) { o_color = base; return; }

    const float size = max(p.p0.x, 1e-6);
    // A razão de aspecto entra antes da malha: com o eixo x medido na mesma
    // unidade do y (unidades de ALTURA), um hexágono é regular no QUADRO e não
    // esticado com a largura dele.
    const float aspect = max(p.texel.y / max(p.texel.x, 1e-9), 1e-9);
    const vec2 c = vec2((v_uv.x - p.p0.y) * aspect, v_uv.y - p.p0.z);
    const float ca = cos(p.p1.x), sa = sin(p.p1.x);
    const vec2 rotated = vec2(c.x * ca - c.y * sa, c.x * sa + c.y * ca);

    const vec2 grid = rotated / size;
    const vec2 r = vec2(1.0, kHexHeight);
    const vec2 h = 0.5 * r;
    const vec2 a1 = mod(grid, r) - h;
    const vec2 a2 = mod(grid - h, r) - h;
    const vec2 gv = (dot(a1, a1) < dot(a2, a2)) ? a1 : a2;
    // Distância até a aresta mais perto, em unidades de célula.
    const vec2 ag = abs(gv);
    const float hexDist = max(dot(ag, vec2(0.5, 0.8660254)), ag.x);
    const float edge = (0.5 - hexDist) * size;          // em uv

    const float halfWidth = 0.5 * max(p.p0.w, 0.0);
    const float w = max(max(p.texel.x, p.texel.y), 1e-9) + max(p.p1.y, 0.0);
    float m = aurea_band_coverage(edge, halfWidth, w);
    m = mix(m, 1.0 - m, step(0.5, p.p1.z));

    const float amount = clamp(m, 0.0, 1.0) * clamp(p.p1.w, 0.0, 1.0) * clamp(p.color.a, 0.0, 1.0);
    const vec3 straight = base.rgb / base.a;
    const vec3 mixed = mix(straight, p.color.rgb, amount);
    o_color = vec4(mixed * base.a, base.a);
}

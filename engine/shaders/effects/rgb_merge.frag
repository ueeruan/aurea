#version 450
// =============================================================================
//  Aurea / shaders / effects / rgb_merge.frag
//
//  Junta três fontes da MESMA camada, uma por canal de cor — o corpo do
//  "RGB no tempo" (Fase 7.3 §25). O vermelho vem da primeira textura, o verde
//  da segunda e o azul da terceira; cada uma foi buscada num instante
//  diferente pelo `prepare`.
//
//  Um passe de tela cheia, sem matriz nenhuma: as três texturas cobrem
//  exatamente a mesma área (a camada inteira), então basta ler o mesmo uv nas
//  três. Fazer isto pelo caminho de composição de camada exigiria uma quad
//  transformada por amostra só para chegar no mesmo lugar.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_tex2;

void main() {
    const vec4 a = texture(u_tex0, v_uv);   // instante do vermelho
    const vec4 b = texture(u_tex1, v_uv);   // instante do verde
    const vec4 c = texture(u_tex2, v_uv);   // instante do azul
    // As três são pré-multiplicadas (espaço de trabalho): somar os canais
    // crus de cada uma reconstrói a cor; o alfa é o maior dos três, que é o
    // que a camada teria se a cor não tivesse sido separada.
    const float alpha = max(a.a, max(b.a, c.a));
    o_color = vec4(a.r, b.g, c.b, alpha);
}

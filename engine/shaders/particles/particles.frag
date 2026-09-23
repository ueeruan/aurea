#version 450
// =============================================================================
//  Aurea / shaders / particles / particles.frag  —  AUREA PARTICULAR
//
//  Quatro formas, escolhidas pelo mesmo canal que a cor já usa (nenhum desvio
//  extra no pipeline): redondo macio, quadrado, quadrado do rastro e brilho
//  radial bem suave. A cor chega PRÉ-MULTIPLICADA do vértice.
//
//  A MACIEZ é um parâmetro do sistema, não uma constante: cristal de gelo e
//  poeira de luz querem bordas opostas.
// =============================================================================
layout(location = 0) in vec2 v_local;
layout(location = 1) in vec4 v_color;
layout(location = 2) in float v_soft;
layout(location = 3) in float v_shape;
layout(location = 0) out vec4 o_color;

void main() {
    // A METRICA e a forma: redondo mede a distancia, quadrado mede o maior
    // eixo. Sem isso o quadrado sairia redondo — a forma chegava ao vertice e
    // morria ali.
    const float d = (v_shape > 0.5 && v_shape < 2.5)
                  ? max(abs(v_local.x), abs(v_local.y))
                  : length(v_local);
    // `v_soft` 0 = borda dura, 1 = bem macia. O padrao do sistema (0,7) cai em
    // ~0,32 de nucleo solido, que e EXATAMENTE a borda que o sistema tinha
    // antes deste controle existir — projeto salvo nao muda de aparencia so
    // porque o controle nasceu.
    const float inner = mix(0.95, 0.05, clamp(v_soft, 0.0, 1.0));
    const float a = 1.0 - smoothstep(inner, 1.0, d);
    if (a <= 0.001) discard;
    o_color = v_color * a;
}

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
//
//  8.2: TEXTURA (imagem do projeto, RGBA8 sRGB de alfa reto, tingida pela cor
//  da partícula), MALHA (iluminação simplificada, ver abaixo) e a CAUDA do
//  rastro com a opacidade própria.
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(location = 0) in vec2 v_local;
layout(location = 1) in vec4 v_color;
layout(location = 2) in float v_soft;
layout(location = 3) in float v_shape;   // 0/3 redondo, 1/2 quadrado, 4 textura, 5 malha
layout(location = 4) in vec4 v_misc;     // opacidade da cauda, é rastro, uv da textura
layout(location = 5) in vec4 v_normal;   // malha: normal (px da camada), iluminada
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex;

// Luz da partícula de malha: UMA direcional fixa, de cima/esquerda e da frente
// (px da camada: y para baixo, z para dentro da tela), + ambiente. As duas
// faces acendem igual (|N·L|): a malha gira livre e o sentido da normal depois
// da rotação não é confiável para decidir frente/verso. É a iluminação
// SIMPLIFICADA do caminho 2D — as luzes e o PBR da cena ficam para o passe 3D.
const vec3 kLight = vec3(-0.36, -0.60, -0.71);
const float kAmbient = 0.30;

void main() {
    if (v_shape > 4.5) {
        float shade = 1.0;
        if (v_normal.w > 0.5) {
            const vec3 n = normalize(v_normal.xyz + vec3(0.0, 0.0, 1e-6));
            shade = kAmbient + (1.0 - kAmbient) * abs(dot(n, normalize(kLight)));
        }
        o_color = vec4(v_color.rgb * shade, v_color.a);
        if (o_color.a <= 0.001) discard;
        return;
    }
    // Cauda do rastro: along = −1 (de onde veio) sai com a opacidade do rastro,
    // a cabeça com a da partícula. Sem rastro (ou opacidade 1) = como antes.
    const float tail = (v_misc.y > 0.5) ? mix(v_misc.x, 1.0, clamp(v_local.y * 0.5 + 0.5, 0.0, 1.0)) : 1.0;
    if (v_shape > 3.5) {
        // Textura: o uv já vem com a proporção da imagem (cabe no quadrado);
        // fora dele, nada.
        const vec2 uv = v_misc.zw;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) discard;
        const vec4 t = texture(u_tex, uv);
        const vec4 lin = vec4(srgb_to_linear(t.rgb) * t.a, t.a);   // pré-multiplicada
        o_color = v_color * lin * tail;
        if (o_color.a <= 0.001) discard;
        return;
    }
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
    o_color = v_color * (a * tail);
}

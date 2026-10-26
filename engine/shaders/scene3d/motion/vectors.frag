#version 450
// =============================================================================
//  Aurea / shaders / scene3d / motion / vectors.frag
//
//  Velocidade de tela da superfície VISÍVEL (o passe testa e grava a própria
//  profundidade, Z reverso): o deslocamento em pixels do alvo entre o começo
//  e o fim do obturador. Saída (RGBA16F):
//      xy = fim − começo (px; y para baixo, a convenção das texturas)
//      z  = profundidade Z reverso do centro (1 perto → 0 infinito)
//      w  = 1 (há superfície; o fundo limpo fica 0)
//  O rastro é limitado a `limits.x` (2× o bloco da reconstrução); o Renderer
//  só escolhe este caminho quando o trajeto medido cabe nele.
// =============================================================================
#include "../../common/bindings.glsl"
#include "vectors_block.glsl"

layout(location = 0) in vec4 v_begin;
layout(location = 1) in vec4 v_end;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in float v_alpha;

layout(location = 0) out vec4 o_velocity;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D t_base;

void main() {
    if (u.alpha.y > 0.5) {
        float a = u.baseColor.a * v_alpha;
        if (u.uvRot.y > 0.5) a *= texture(t_base, v_uv).a;
        if (a < u.alpha.x) discard;
    }
    vec2 velocity = vec2(0.0);
    // Uma ponta atrás da câmera não tem posição de tela: sem rastro (o
    // Renderer manda esse caso para a acumulação).
    if (v_begin.w > 1e-4 && v_end.w > 1e-4) {
        velocity = (v_end.xy / v_end.w - v_begin.xy / v_begin.w) * u.uvRot.zw;
        float len = length(velocity);
        if (!(len <= u.limits.x)) velocity = len > 0.0 && len < 1e30 ? velocity * (u.limits.x / len) : vec2(0.0);
    }
    o_velocity = vec4(velocity, gl_FragCoord.z, 1.0);
}

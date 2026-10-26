#version 450
// =============================================================================
//  Aurea / shaders / scene3d / motion / vectors_sky.frag
//
//  Velocidade do FUNDO visível (céu do ambiente): um ponto no infinito só se
//  move com a ROTAÇÃO da câmera. As duas matrizes 3×3 levam o NDC do centro
//  do obturador ao (x·w, y·w, w) do começo e do fim (ver
//  velocity_sky_reprojection). Desenhado primeiro no passe de velocidade, sem
//  profundidade: a geometria escreve por cima. w = 0 marca "fundo" (infinito).
// =============================================================================
#include "../../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_velocity;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    mat4 begin;     // colunas 0..2 = a 3×3
    mat4 end;
    vec4 target;    // xy = metade do tamanho do alvo (px), z = maior rastro (px)
} p;

void main() {
    vec3 ndc = vec3(v_uv * 2.0 - 1.0, 1.0);
    vec3 b = mat3(p.begin) * ndc;
    vec3 e = mat3(p.end) * ndc;
    vec2 velocity = vec2(0.0);
    if (b.z > 1e-6 && e.z > 1e-6) {
        velocity = (e.xy / e.z - b.xy / b.z) * p.target.xy;
        float len = length(velocity);
        if (!(len <= p.target.z)) velocity = len > 0.0 && len < 1e30 ? velocity * (p.target.z / len) : vec2(0.0);
    }
    o_velocity = vec4(velocity, 0.0, 0.0);
}

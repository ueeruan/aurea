#version 450
// =============================================================================
//  Aurea / shaders / scene3d / ground.vert
//
//  O chão do grupo 3D: um quadrado horizontal (normal "para cima" = −Y) em
//  y = center.y, centrado sob os modelos. Sem buffer de vértice: 6 vértices
//  gerados aqui (dois triângulos).
// =============================================================================
#include "common/ground.glsl"

layout(location = 0) out vec3 v_world;

void main() {
    const vec2 corners[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    vec2 q = corners[gl_VertexIndex];
    vec3 w = vec3(g.center.x + q.x * g.center.w, g.center.y, g.center.z + q.y * g.center.w);
    v_world = w;
    gl_Position = g.viewProj * vec4(w, 1.0);
}

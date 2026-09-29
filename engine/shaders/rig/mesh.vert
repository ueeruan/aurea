#version 450
// =============================================================================
//  Aurea / shaders / rig / mesh.vert
//
//  Rig 2D: a malha da imagem já deformada pela pose na CPU
//  (timeline/Rig.cpp), lida do buffer do quadro — 1 vec4 por vértice:
//  (x, y) = posição na textura da camada (caixa da pose), (u, v) = ponto da
//  imagem original. Triângulos soltos (3 vértices cada).
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_DATA, std430) readonly buffer Data { vec4 d[]; } data;

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 params;   // x = primeiro vec4 da malha
} pc;

layout(location = 0) out vec2 v_uv;

void main() {
    const vec4 a = data.d[int(pc.params.x) + gl_VertexIndex];
    v_uv = a.zw;
    gl_Position = pc.clipFromLayer * vec4(a.xy, 0.0, 1.0);
}

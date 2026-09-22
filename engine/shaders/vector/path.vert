#version 450
// =============================================================================
//  Aurea / shaders / vector / path.vert
//
//  Camada vetorial: triângulos prontos da CPU (vector/Vector.cpp), lidos do
//  buffer do quadro — 2 vec4 por vértice: (x, y, gx, gy) = posição na textura
//  da camada e no espaço do degradê; (d, tinta, alfa, _) = distância assinada
//  até a borda (px da camada, + dentro; interior = 1e4), índice da tinta e o
//  alfa da cópia/grupo.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_DATA, std430) readonly buffer Data { vec4 d[]; } data;

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 params;   // x = primeiro vec4 dos vértices, y = primeiro vec4 das tintas
} pc;

layout(location = 0) out vec2 v_grad;
layout(location = 1) out float v_dist;
layout(location = 2) flat out int v_paint;
layout(location = 3) flat out float v_alpha;

void main() {
    const int base = int(pc.params.x) + gl_VertexIndex * 2;
    const vec4 a = data.d[base];
    const vec4 b = data.d[base + 1];
    v_grad = a.zw;
    v_dist = b.x;
    v_paint = int(pc.params.y) + int(b.y + 0.5) * 12;
    v_alpha = b.z;
    gl_Position = pc.clipFromLayer * vec4(a.xy, 0.0, 1.0);
}

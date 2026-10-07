#version 450
// =============================================================================
//  Aurea / shaders / effects / mesh_warp.vert
//
//  Malha de deformação: a grade tesselada na CPU (um texel RGBA32F por ponto,
//  posição em px da camada) vira triângulos sem vertex buffer — 6 vértices
//  por célula. O uv de cada ponto é o da grade REGULAR na fonte.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a grade deformada

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;       // região de saída (px da camada)
    vec4 inputRegion;  // região da entrada
    vec4 grid;         // tamanho da camada (px), pontos por eixo
} p;

layout(location = 0) out vec2 v_uv;

void main() {
    const ivec2 corners[6] = ivec2[](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(0, 1), ivec2(1, 0), ivec2(1, 1));
    int gx = int(p.grid.z + 0.5);
    int gy = int(p.grid.w + 0.5);
    int cell = gl_VertexIndex / 6;
    ivec2 g = ivec2(cell % (gx - 1), cell / (gx - 1)) + corners[gl_VertexIndex % 6];
    vec2 point = texelFetch(u_tex1, g, 0).xy;
    vec2 source = vec2(g) / vec2(float(gx - 1), float(gy - 1)) * p.grid.xy;
    v_uv = (source - p.inputRegion.xy) / p.inputRegion.zw;
    vec2 clip = (point - p.region.xy) / p.region.zw * 2.0 - 1.0;
    gl_Position = vec4(clip, 0.0, 1.0);
}

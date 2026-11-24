#version 450
// =============================================================================
//  Aurea / shaders / effects / face_rig_part.vert
//
//  Parte do Rig 2D separado (aurea.rig.part): a grade deformada na CPU
//  (effects/FaceRigParts.cpp) chega como textura de dado RGBA32F — um texel
//  por vértice, linha a linha: xy = posição deformada, zw = repouso (px da
//  camada). gl_VertexIndex escolhe a célula (6 vértices, dois triângulos).
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a grade

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;       // região de saída (px da camada)
    vec4 inputRegion;  // região da entrada
    vec4 info;         // x = vértices por linha, y = largura da textura de dado
    vec4 eye;
    vec4 lid;
    vec4 mouth;
    vec4 mouthK;
    vec4 mouthUv;
} p;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec2 v_rest;

void main() {
    const int nx = max(2, int(p.info.x + 0.5));
    const int tw = max(1, int(p.info.y + 0.5));
    const int cell = gl_VertexIndex / 6;
    const int corner = gl_VertexIndex - cell * 6;
    // Cantos: (0,0) (1,0) (0,1) | (1,0) (1,1) (0,1).
    const int ox = (corner == 1 || corner == 3 || corner == 4) ? 1 : 0;
    const int oy = (corner == 2 || corner == 4 || corner == 5) ? 1 : 0;
    const int gx = cell % (nx - 1) + ox;
    const int gy = cell / (nx - 1) + oy;
    const int k = gy * nx + gx;
    const vec4 t = texelFetch(u_tex1, ivec2(k % tw, k / tw), 0);
    v_rest = t.zw;
    v_uv = (t.zw - p.inputRegion.xy) / p.inputRegion.zw;
    const vec2 clip = (t.xy - p.region.xy) / p.region.zw * 2.0 - 1.0;
    gl_Position = vec4(clip, 0.0, 1.0);
}

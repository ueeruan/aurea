#version 450
// =============================================================================
//  Aurea / shaders / effects / puppet.vert
//
//  Fantoche: a malha resolvida na CPU (ARAP, effects/Puppet.cpp) chega como
//  textura de dado RGBA32F — um texel por vértice de triângulo, em ordem:
//  xy = posição deformada, zw = posição de repouso (px da camada). Sem
//  vertex buffer: gl_VertexIndex escolhe o texel.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a malha

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;       // região de saída (px da camada)
    vec4 inputRegion;  // região da entrada
    vec4 info;         // x = largura da textura de dado
} p;

layout(location = 0) out vec2 v_uv;

void main() {
    const int w = max(1, int(p.info.x + 0.5));
    const vec4 t = texelFetch(u_tex1, ivec2(gl_VertexIndex % w, gl_VertexIndex / w), 0);
    v_uv = (t.zw - p.inputRegion.xy) / p.inputRegion.zw;
    const vec2 clip = (t.xy - p.region.xy) / p.region.zw * 2.0 - 1.0;
    gl_Position = vec4(clip, 0.0, 1.0);
}

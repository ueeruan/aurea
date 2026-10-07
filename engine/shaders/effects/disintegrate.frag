#version 450
// =============================================================================
//  Aurea / shaders / effects / disintegrate.frag
//
//  Um fragmento solto do Desintegrar: o quadrado da camada que ele levou
//  (amostrado no uv dele), com um texel de antialiasing na borda, o brilho da
//  borda que esfria com a idade e a opacidade do esmaecer. Pré-multiplicado.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 src;
    vec4 dst;
    vec4 grid;
    vec4 front;
    vec4 timing;
    vec4 motion;
    vec4 glow;     // cor do brilho (linear), intensidade
    vec4 texel;
    vec4 uvMap;
    vec4 mode;
} p;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_local;
layout(location = 2) flat in vec4 v_info;
layout(location = 0) out vec4 o_color;

void main() {
    const float edge = max(abs(v_local.x), abs(v_local.y));
    const float cov = clamp((1.0 - edge) * v_info.z + 0.5, 0.0, 1.0);
    if (cov <= 0.0) discard;
    const vec4 s = texture(u_tex0, v_uv);
    const vec3 rgb = s.rgb + p.glow.rgb * v_info.y * s.a;
    o_color = vec4(rgb, s.a) * (v_info.x * cov);
}

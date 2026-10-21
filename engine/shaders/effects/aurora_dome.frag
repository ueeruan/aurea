#version 450
// =============================================================================
//  Aurea / shaders / effects / aurora_dome.frag
//
//  Aurora, passe 1 (AuroraEffect.cpp): a CÚPULA. Cada texel é uma direção
//  do céu (aurora_dome_dir) e guarda só a luz da aurora nela — as dezenas de
//  lâminas de ruído, em resolução reduzida e só na faixa que a câmera vê.
//  O passe 2 (aurora.frag) lê daqui o céu e o reflexo.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"
#include "aurora_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

void main() {
    // Ruído de gradiente intercalado, PARADO no tempo: desloca as lâminas por
    // texel sem cintilar de um quadro para o outro.
    const float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    o_color = vec4(aurora_light(aurora_dome_dir(v_uv), p.p3.x, jitter), 1.0);
}

#version 450
// =============================================================================
//  Aurea / shaders / video / flow_pack.frag
//
//  Junta os dois fluxos do nível base numa textura só (a do cache): RG = do
//  quadro atual para o seguinte, BA = do seguinte para o atual. O desfoque
//  vetorial lê só o RG.
//
//  Num corte de cena (flow_cut.frag) os dois quadros não têm relação e qualquer
//  vetor é lixo: o campo inteiro sai com o valor reservado (> 5000, nenhum
//  fluxo real chega perto) e quem lê não usa vetor nenhum.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_forward;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_backward;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_cut;      // R = 1: corte de cena

void main() {
    if (textureLod(u_cut, vec2(0.5), 0.0).r > 0.5) {
        o_color = vec4(10000.0);
        return;
    }
    o_color = vec4(textureLod(u_forward, v_uv, 0.0).rg, textureLod(u_backward, v_uv, 0.0).rg);
}

#version 450
// =============================================================================
//  Aurea / shaders / scene3d / post / volume_apply.frag
//
//  Aplica a neblina volumétrica (volume.frag, meia resolução) na cena HDR:
//  cena × transmissão + luz espalhada. O HDR da cena vem comprimido acima
//  de 1 (common/hdr.glsl): decodifica, mistura e codifica de volta, antes do
//  DOF, do bloom e do tone map. A meia resolução sobe com 4 amostras
//  pesadas pela transmissão vizinha (sem halo na borda dos objetos).
// =============================================================================
#include "../../common/bindings.glsl"
#include "../common/hdr.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D t_scene;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D t_vol;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho do volume (meia res)
} p;

void main() {
    vec4 s = textureLod(t_scene, v_uv, 0.0);
    vec4 v = textureLod(t_vol, v_uv, 0.0);
    // Junta 4 vizinhos com transmissão parecida (borda nítida do objeto).
    vec4 sum = v; float wsum = 1.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = vec2(i == 0 ? 1.0 : i == 1 ? -1.0 : 0.0, i == 2 ? 1.0 : i == 3 ? -1.0 : 0.0) * p.texel.xy;
        vec4 n = textureLod(t_vol, v_uv + o, 0.0);
        float w = exp(-abs(n.a - v.a) * 16.0);
        sum += n * w; wsum += w;
    }
    v = sum / wsum;
    vec3 scene = aurea_hdr_decode(s.rgb);
    // Fundo transparente (sem céu): a neblina acompanha a cobertura da cena.
    vec3 fogged = scene * v.a + v.rgb * s.a;
    o_color = vec4(aurea_hdr_encode(fogged), s.a);
}

#version 450
// =============================================================================
//  Aurea / shaders / scene3d / post / dof_coc.frag
//
//  Profundidade de campo, passo 1: o CÍRCULO DE CONFUSÃO de cada pixel (lente
//  fina, ver SceneRenderer.hpp `DofLens`), da profundidade Z reversa:
//      raio(d) = K · (1 − (S1/nearZ) · d)     negativo = perto, positivo = longe
//
//  A profundidade é a MAIS PERTO (maior d) do 3×3: o pixel de borda do MSAA
//  (meio objeto, meio fundo — o resolve da profundidade guarda só a amostra 0)
//  conta como o objeto. Sem isso, a borda antisserrilhada de um sujeito em foco
//  entraria no gather como "fundo desfocado" e faria um halo em volta dele.
//
//  Saída: r = raio com sinal (px), g = profundidade usada (para o "mais longe
//  que o centro" do gather). RGBA16F, lido com amostragem pontual.
// =============================================================================
#include "../../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_coc;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_depth;   // Z reverso, 1 amostra

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho, z = raio máximo (px)
    vec4 lens;    // x = K (raio no infinito, px), y = S1/nearZ
} p;

void main() {
    float d = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            d = max(d, textureLod(u_depth, v_uv + vec2(float(x), float(y)) * p.texel.xy, 0.0).r);
    const float r = clamp(p.lens.x * (1.0 - p.lens.y * d), -p.texel.z, p.texel.z);
    o_coc = vec4(r, d, 0.0, 1.0);
}

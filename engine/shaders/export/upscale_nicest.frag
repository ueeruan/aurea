#version 450
// =============================================================================
//  Aurea / shaders / export / upscale_nicest.frag
//
//  Upscaler do export (0.1.7, substitui o upscale por rede neural na CPU):
//  a composição renderizada em W/S × H/S vira W × H (S = 2 ou 4) num passe
//  só, na GPU, antes do NV12.
//
//  Espacial — textureNicest: a coordenada é empurrada para o centro do texel
//  com uma rampa suave (smoothstep do miolo 0,2..0,8 de cada texel) e o
//  amostrador BILINEAR da GPU faz o resto: bordas mais firmes que o bilinear
//  puro, sem o serrilhado do vizinho mais próximo.
//
//  Temporal (opcional) — TAA_MODE 4: mistura com o quadro anterior já
//  ampliado, blend = 0,5·exp(−|v|²). Não há vetores de movimento confiáveis
//  num quadro de vídeo/composição qualquer: a "velocidade" é a diferença do
//  pixel entre os dois quadros (× sensibilidade). Onde o pixel muda (movimento,
//  corte, troca de mídia) o peso cai a ~0 e vale só o espacial — sem rastro;
//  onde ele está parado, a mistura tira o tremor/cintilação do ampliado.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 res;   // largura, altura da composição (texels), 1/largura, 1/altura
    vec4 cfg;   // x = temporal ligado, y = histórico válido, z = sensibilidade do movimento
} p;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;   // composição (bilinear)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_prev;  // quadro anterior ampliado

vec4 textureNicest(sampler2D tex, vec2 uv, vec2 resolution) {
    uv = uv * resolution + 0.5;
    vec2 iuv = floor(uv);
    vec2 fuv = fract(uv);
    fuv = clamp((fuv - 0.2) / 0.6, 0.0, 1.0);
    uv = iuv + fuv * fuv * (3.0 - 2.0 * fuv);
    uv = (uv - 0.5) / resolution;
    return texture(tex, uv);
}

float len2(vec2 v) { return dot(v, v); }

void main() {
    vec4 color = textureNicest(u_src, v_uv, p.res.xy);
    if (p.cfg.x > 0.5 && p.cfg.y > 0.5) {
        vec4 prevColor = texture(u_prev, v_uv);
        // "Velocidade" = mudança do pixel (cor e cobertura) entre os quadros.
        vec3 d = color.rgb - prevColor.rgb;
        vec2 velocity = vec2(dot(abs(d), vec3(0.2126, 0.7152, 0.0722)), abs(color.a - prevColor.a)) * p.cfg.z;
        float blendFactor = 0.5 * exp(-len2(velocity));
        color = mix(color, prevColor, blendFactor);
    }
    o_color = color;
}

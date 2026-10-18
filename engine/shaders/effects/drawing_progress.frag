#version 450
// =============================================================================
//  Aurea / shaders / effects / drawing_progress.frag
//
//  Progresso do desenho FORA da camada vetorial (PNG, vídeo, texto, desenho
//  sem dado vetorial): não há traço para seguir, então a camada aparece numa
//  varredura na direção pedida, com a ponta suave. A "tinta" — pixel escuro e
//  opaco — chega antes do papel claro: o pixel claro fica `atraso` da
//  varredura para trás. Na camada vetorial o corte é na geometria (C++).
//
//  0% = alfa 0 (sinal em p0.w); 100% = camada exata (o efeito sai da cadeia).
//
//  p0 = frente, largura da ponta, atraso do papel (frações da varredura), tudo apagado (0/1)
//  p1 = centro da caixa da camada (ou da parede do Motion Tile) em px, direção da varredura (unitária)
//  p2 = comprimento da varredura (px)
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const vec4 src = cc_at(px);
    if (p.p0.w > 0.5) { o_color = vec4(0.0); return; }
    // Posição na varredura: 0 onde ela começa, 1 onde termina.
    const float s = clamp(dot(px - p.p1.xy, p.p1.zw) / max(p.p2.x, 1e-3) + 0.5, 0.0, 1.0);
    // Tinta: escuro e opaco (o brilho que o olho vê, sem o alfa).
    const vec3 rgb = src.a > 1e-4 ? src.rgb / src.a : vec3(0.0);
    const float light = aurea_linear_to_srgb(clamp(aurea_luma(max(rgb, vec3(0.0))), 0.0, 1.0));
    const float ink = clamp(src.a * (1.0 - light), 0.0, 1.0);
    const float k = s + p.p0.z * (1.0 - ink);
    // Fica o que a frente já passou; a ponta é a rampa de largura p0.y.
    const float keep = smoothstep(0.0, 1.0, (p.p0.x - k) / max(p.p0.y, 1e-4));
    o_color = src * keep;
}

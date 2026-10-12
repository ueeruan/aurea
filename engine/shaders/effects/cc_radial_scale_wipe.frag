#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_radial_scale_wipe.frag
//
//  Varredura radial com escala (o comportamento do CC Radial ScaleWipe): um
//  furo abre no "Centro" e empurra a imagem para fora — o que estava entre o
//  centro e a borda é espremido no anel que sobra, até sumir pela borda.
//  "Inverter direção": o disco visível encolhe para o centro levando a imagem
//  inteira reduzida dentro dele.
//  0% é a camada exata; 100% é vazio.
//
//  p0 = centro (px), conclusão (0..1), raio que cobre a camada (px); p1.x = inverter.
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const vec2 d = px - p.p0.xy;
    const float r = length(d);
    const float maxR = max(p.p0.w, 1.0);
    const float k = clamp(p.p0.z, 0.0, 1.0);
    const vec2 dirv = r > 1e-4 ? d / r : vec2(0.0);
    if (p.p1.x > 0.5) {
        const float visible = (1.0 - k) * maxR;
        const float cov = clamp((visible - r) / cc_texel() + 0.5, 0.0, 1.0);
        if (cov <= 0.0 || visible <= 1e-3) { o_color = vec4(0.0); return; }
        o_color = cc_at(p.p0.xy + dirv * (r * maxR / visible)) * cov;
        return;
    }
    const float hole = k * maxR;
    const float cov = clamp((r - hole) / cc_texel() + 0.5, 0.0, 1.0);
    if (cov <= 0.0 || hole >= maxR) { o_color = vec4(0.0); return; }
    const float srcR = max(r - hole, 0.0) * maxR / (maxR - hole);
    o_color = cc_at(p.p0.xy + dirv * srcR) * cov;
}

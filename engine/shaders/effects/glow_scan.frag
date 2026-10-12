#version 450
// =============================================================================
//  Aurea / shaders / effects / glow_scan.frag
//
//  Glow Scan: uma faixa de luz atravessa a camada. Dentro da faixa o
//  conteúdo acende (ou só o contorno) e um brilho sai das bordas para fora.
//  Uma passada só: halo por 16 amostras em dois anéis em volta do pixel.
// =============================================================================
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel;
    vec4 p0;     // centro da camada x, y (px), eixo cos, sen
    vec4 p1;     // posição da faixa no eixo (px), meia largura (px), suavidade 0..1, intensidade
    vec4 p2;     // região da saída (px da camada)
    vec4 p3;     // raio do brilho (px), só nas bordas (0/1), brilho em volta, -
    vec4 color;
} p;

float alpha_at(vec2 uv) {
    const vec2 src = uv * p.uvMap.xy + p.uvMap.zw;
    if (any(lessThan(src, vec2(0.0))) || any(greaterThan(src, vec2(1.0)))) return 0.0;
    return texture(u_tex0, src).a;
}

void main() {
    const vec2 point = p.p2.xy + v_uv * p.p2.zw;
    const float along = dot(point - p.p0.xy, p.p0.zw) - p.p1.x;
    const float hw = max(p.p1.y, 1e-3);
    const float band = 1.0 - smoothstep(hw * (1.0 - clamp(p.p1.z, 0.0, 0.999)), hw, abs(along));
    const vec4 src = texture(u_tex0, v_uv * p.uvMap.xy + p.uvMap.zw);
    if (band <= 0.0) { o_color = src; return; }

    // Halo: média do alfa em dois anéis (raio inteiro e metade).
    const vec2 step = vec2(p.p3.x) / max(p.p2.zw, vec2(1.0));
    float halo = 0.0;
    for (int i = 0; i < 8; ++i) {
        const float a = float(i) * 0.785398163 + 0.39;
        const vec2 dir = vec2(cos(a), sin(a));
        halo += alpha_at(v_uv + dir * step) * 0.6 + alpha_at(v_uv + dir * step * 0.5) * 0.4;
    }
    halo /= 8.0;
    // Contorno: o alfa daqui menos o de perto (meio raio).
    const float edge = clamp(src.a - min(halo * 1.2, 1.0) * 0.85, 0.0, 1.0) + clamp(halo - src.a, 0.0, 1.0) * 0.5;
    const float core = p.p3.y > 0.5 ? edge : src.a;
    const float shine = band * core * p.p1.w;
    const float glow = band * halo * (1.0 - src.a * 0.7) * p.p3.z;
    const vec3 light = p.color.rgb * (shine + glow);
    o_color = vec4(src.rgb + light, clamp(src.a + glow * p.color.a, 0.0, 1.0));
}

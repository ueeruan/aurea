#version 450
// =============================================================================
//  Aurea / shaders / effects / stroke_distance.frag
//
//  Contorno da silhueta, passe 1 (horizontal): para cada texel da saída, a
//  distância NA LINHA até o pixel opaco mais próximo (R) e até o transparente
//  mais próximo (G), em px da camada. O passe 2 fecha a distância euclidiana
//  com min(R² + dy²) coluna acima e abaixo.
//
//  "Opaco" é alfa ≥ 0,5; o quanto o alfa passa (ou falta) de 0,5 desloca a
//  borda em fração de texel — o campo sai antialiasado, não em degraus.
//  A saída cobre a região EXPANDIDA: fora da imagem de entrada o alfa é 0.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = amostras para cada lado (texels desta saída), y = px por texel desta saída, z = alcance (px)
    vec4 p1;
    vec4 p2;   // região da ENTRADA (px da camada)
    vec4 p3;   // região da SAÍDA (px da camada)
    vec4 color;
} p;

float alpha_at(vec2 point) {
    const vec2 uv = (point - p.p2.xy) / max(p.p2.zw, vec2(1e-4));
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 0.0;
    return texture(u_tex0, uv).a;
}

void main() {
    const vec2 point = p.p3.xy + v_uv * p.p3.zw;
    const int taps = int(clamp(p.p0.x, 1.0, 48.0));
    const float px = max(p.p0.y, 1e-4);
    const float far = max(p.p0.z, 1.0) * 4.0;
    float toOpaque = far;
    float toClear = far;
    for (int dx = -48; dx <= 48; ++dx) {
        if (dx < -taps || dx > taps) continue;
        const float a = alpha_at(point + vec2(float(dx) * px, 0.0));
        const float d = abs(float(dx));
        if (a >= 0.5) toOpaque = min(toOpaque, max(d - (a - 0.5), 0.0) * px);
        else          toClear  = min(toClear,  max(d - (0.5 - a), 0.0) * px);
    }
    o_color = vec4(toOpaque, toClear, 0.0, 1.0);
}

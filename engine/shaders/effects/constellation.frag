#version 450
// =============================================================================
//  Aurea / shaders / effects / constellation.frag
//
//  Uma primitiva da Constelação (ver constellation.vert): face chapada,
//  linha com cobertura analítica através da largura (1 texel de borda) ou
//  ponto redondo com borda suave. Saída pré-multiplicada (o passe mistura
//  "sobre" ou soma).
// =============================================================================
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_coord;
layout(location = 2) flat in vec4 v_shape;
layout(location = 0) out vec4 o_color;

void main() {
    float a = v_color.a;
    if (v_shape.x > 1.5) {
        // Ponto: suavidade 0 = disco duro com 1 texel de antisserrilhado;
        // 1 = mancha que cai desde o centro.
        const float r = max(v_shape.y, 1e-3);
        const float inner = r * (1.0 - clamp(v_shape.z, 0.0, 1.0));
        a *= 1.0 - smoothstep(inner - 0.5, r + 0.5, length(v_coord));
    } else if (v_shape.x > 0.5) {
        // Linha: v_coord = (meia largura, distância ao eixo), em texels.
        a *= clamp(v_coord.x + 0.5 - abs(v_coord.y), 0.0, 1.0);
    }
    if (a <= 0.0) discard;
    o_color = vec4(v_color.rgb * a, a);
}

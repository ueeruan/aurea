#version 450
// =============================================================================
//  Aurea / shaders / effects / particular.frag
//
//  Uma partícula do Particular: disco com borda suave. Suavidade 1 = mancha
//  que cai desde o centro; 0 = disco duro (o miolo para em 0,95 porque
//  smoothstep(1, 1, x) não é definido). Saída pré-multiplicada.
// =============================================================================
layout(location = 0) in vec2 v_corner;
layout(location = 1) in vec4 v_color;
layout(location = 2) flat in float v_feather;
layout(location = 0) out vec4 o_color;

void main() {
    const float d = length(v_corner);
    const float core = clamp(1.0 - v_feather, 0.0, 0.95);
    const float a = v_color.a * (1.0 - smoothstep(core, 1.0, d));
    if (a <= 0.0) discard;
    o_color = vec4(v_color.rgb * a, a);
}

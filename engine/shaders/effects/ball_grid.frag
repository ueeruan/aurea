#version 450
// =============================================================================
//  Aurea / shaders / effects / ball_grid.frag
//
//  A bola: o quadrado do vértice vira um disco com a normal de uma esfera,
//  luz difusa de cima à esquerda (à frente) e um brilho especular. A cor é a
//  da camada na célula; a borda tem um texel de antialiasing.
// =============================================================================
layout(location = 0) in vec2 v_local;
layout(location = 1) flat in vec4 v_color;
layout(location = 2) flat in float v_radius;
layout(location = 0) out vec4 o_color;

void main() {
    const float d = length(v_local);
    const float cov = clamp((1.0 - d) * v_radius + 0.5, 0.0, 1.0);
    if (cov <= 0.0) discard;
    const float r = min(d, 1.0);
    const vec2 xy = d > 1e-5 ? v_local / d * r : vec2(0.0);
    const vec3 n = vec3(xy, sqrt(max(1.0 - r * r, 0.0)));
    const vec3 L = normalize(vec3(-0.45, -0.55, 0.70));     // y da camada cresce para baixo
    const float diffuse = max(dot(n, L), 0.0);
    const float spec = pow(max(dot(n, normalize(L + vec3(0.0, 0.0, 1.0))), 0.0), 32.0) * 0.35;
    const vec3 col = v_color.rgb * (0.28 + 0.72 * diffuse) + vec3(spec);
    const float a = v_color.a * cov;
    o_color = vec4(clamp(col, 0.0, 1.0) * a, a);
}

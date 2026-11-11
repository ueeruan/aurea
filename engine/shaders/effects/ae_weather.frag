#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_weather.frag
//
//  Lote "AE 2026", clima procedural sobre a camada. Determinístico pela
//  posição e pelo tempo (u.layer.z, s): preview == export. Três planos de
//  profundidade; a "profundidade da origem" afasta os de trás (menores, mais
//  lentos, mais apagados). u.p[11].w escolhe:
//    0 Chuva — p0: x = quantidade, y = velocidade, z = ângulo (rad),
//      w = variação do ângulo (rad); p1: x = tamanho da gota, y = profundidade,
//      z = opacidade.
//    1 Neve — p0: x = quantidade, y = velocidade, z = amplitude, w = frequência;
//      p1: x = tamanho do floco (px), y = profundidade, z = opacidade.
// =============================================================================
#include "ae_common.glsl"

vec2 rot(vec2 v, float a) {
    const float c = cos(a), s = sin(a);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

float rain_layer(vec2 px, float plane, float t) {
    const float depth = clamp(u.p[1].y, 0.0, 1.0);
    const float sc = 1.0 - plane * 0.3 * (0.4 + depth);          // perto = 1
    const vec2 cellSize = vec2(18.0, 64.0) * sc;
    const float density = clamp(u.p[0].x / 1200.0, 0.0, 1.0);
    vec2 q = rot(px, -u.p[0].z);
    q.y -= t * u.p[0].y * 1100.0 * sc;
    const vec2 id = floor(q / cellSize);
    const vec2 f = q / cellSize - id;
    const float salt = 31.0 + plane * 17.0;
    if (ae_hash(id, salt) > density) return 0.0;
    const float x0 = 0.15 + 0.7 * ae_hash(id, salt + 1.0);
    const float y0 = ae_hash(id, salt + 2.0) * 0.4;
    const float lean = (ae_hash(id, salt + 3.0) - 0.5) * 2.0 * tan(u.p[0].w);
    const float along = (f.y - y0) / 0.6;                       // 0..1 na gota
    if (along < 0.0 || along > 1.0) return 0.0;
    const float x = x0 + lean * (f.y - y0) * cellSize.y / cellSize.x;
    const float width = max(u.p[1].x * 0.7 * sc, 0.35) / cellSize.x;
    const float across = abs(f.x - x) / width;
    const float body = 1.0 - smoothstep(0.5, 1.0, across);
    return body * along * along * (0.45 + 0.55 * sc);
}

float snow_layer(vec2 px, float plane, float t) {
    const float depth = clamp(u.p[1].y, 0.0, 1.0);
    const float sc = 1.0 - plane * 0.3 * (0.4 + depth);
    const float cs = 40.0 * sc;
    const float density = clamp(u.p[0].x / 1200.0, 0.0, 1.0);
    vec2 q = px;
    q.y -= t * u.p[0].y * 120.0 * sc;
    const vec2 id0 = floor(q / cs);
    float acc = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const vec2 id = id0 + vec2(float(dx), float(dy));
            const float salt = 71.0 + plane * 13.0;
            if (ae_hash(id, salt) > density) continue;
            const float ph = ae_hash(id, salt + 1.0) * AUREA_TAU;
            const float sway = sin(t * u.p[0].w * 2.0 + ph) * u.p[0].z * 10.0 * sc;
            const vec2 c = (id + vec2(ae_hash(id, salt + 2.0), ae_hash(id, salt + 3.0))) * cs + vec2(sway, 0.0);
            const float r = max(u.p[1].x * sc * (0.6 + 0.6 * ae_hash(id, salt + 4.0)), 0.5);
            acc += 1.0 - smoothstep(r * 0.45, r, length(q - c));
        }
    }
    return min(acc, 1.0) * (0.5 + 0.5 * sc);
}

void main() {
    const vec4 src = texture(u_tex0, ae_in_uv());
    const vec2 px = ae_px();
    const float t = u.layer.z;
    const bool snow = u.p[11].w > 0.5;
    float k = 0.0;
    for (int i = 0; i < 3; ++i) k += snow ? snow_layer(px, float(i), t) : rain_layer(px, float(i), t);
    k = clamp(k, 0.0, 1.0) * clamp(u.p[1].z, 0.0, 1.0);
    const vec3 col = snow ? vec3(1.0) : vec3(0.78, 0.82, 0.88);
    // A partícula clara sobre a camada (também onde ela é transparente).
    o_color = vec4(col * k, k) + src * (1.0 - k);
}

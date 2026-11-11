#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_vr.frag
//
//  Vídeo imersivo, lote "AE 2026" (AeEffects2026Extra.cpp): Girar esfera VR,
//  Esfera para plano VR, Plano para esfera VR e Conversor VR — a MESMA conta:
//  ponto da saída → direção (projeção de saída) → rotação → ponto da entrada
//  (projeção de entrada). Fora da projeção = transparente.
//
//  Projeções (contrato com o C++): 0 equiretangular · 1 cubo 3x2 · 2 olho de
//  peixe 180° (angular) · 3 plano retilíneo (campo de visão).
//  Cubo 3x2 do Aurea: em cima esquerda/frente/direita, embaixo baixo/trás/cima.
//  Direção: x direita, y cima, z frente.
//
//  u.p[0]: x = projeção de entrada, y = projeção de saída,
//          z = tan(½ campo de visão) do plano de entrada, w = do plano de saída
//  u.p[1..3].xyz: linhas da rotação (direção da saída → direção da entrada)
// =============================================================================
#include "ae_common.glsl"

const float PI = 3.14159265359;

/// uv (0..1, y para baixo) → direção. `ok` falso: fora da projeção.
vec3 to_dir(int proj, vec2 uv, float tanHalf, out bool ok) {
    ok = true;
    if (proj == 0) {
        const float lon = (uv.x - 0.5) * 2.0 * PI;
        const float lat = (0.5 - uv.y) * PI;
        return vec3(cos(lat) * sin(lon), sin(lat), cos(lat) * cos(lon));
    }
    if (proj == 1) {
        const vec2 cell = floor(uv * vec2(3.0, 2.0));
        const vec2 f = fract(uv * vec2(3.0, 2.0)) * 2.0 - 1.0;   // −1..1 na face, y para baixo
        const float a = f.x, b = -f.y;
        const int face = int(cell.y) * 3 + int(clamp(cell.x, 0.0, 2.0));
        if (face == 0) return normalize(vec3(-1.0, b, a));    // esquerda
        if (face == 1) return normalize(vec3(a, b, 1.0));     // frente
        if (face == 2) return normalize(vec3(1.0, b, -a));    // direita
        if (face == 3) return normalize(vec3(a, -1.0, b));    // baixo
        if (face == 4) return normalize(vec3(-a, b, -1.0));   // trás
        return normalize(vec3(a, 1.0, -b));                   // cima
    }
    if (proj == 2) {
        const vec2 p = uv * 2.0 - 1.0;
        const float r = length(p);
        if (r > 1.0) { ok = false; return vec3(0.0, 0.0, 1.0); }
        const float th = r * PI * 0.5;
        const vec2 dir2 = r > 1e-6 ? p / r : vec2(0.0);
        return vec3(sin(th) * dir2.x, -sin(th) * dir2.y, cos(th));
    }
    const float aspect = ae_layer_size().y / ae_layer_size().x;
    const vec2 p = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0) * vec2(tanHalf, tanHalf * aspect);
    return normalize(vec3(p, 1.0));
}

/// Direção → uv da projeção. `ok` falso: a direção não cai nela.
vec2 to_uv(int proj, vec3 d, float tanHalf, out bool ok) {
    ok = true;
    if (proj == 0) {
        const float lon = atan(d.x, d.z);
        const float lat = asin(clamp(d.y, -1.0, 1.0));
        return vec2(fract(lon / (2.0 * PI) + 0.5), 0.5 - lat / PI);
    }
    if (proj == 1) {
        const vec3 a = abs(d);
        vec2 f;
        float col, row;
        if (a.x >= a.y && a.x >= a.z) {
            if (d.x < 0.0) { f = vec2(d.z, d.y) / a.x; col = 0.0; row = 0.0; }
            else { f = vec2(-d.z, d.y) / a.x; col = 2.0; row = 0.0; }
        } else if (a.y >= a.z) {
            if (d.y < 0.0) { f = vec2(d.x, d.z) / a.y; col = 0.0; row = 1.0; }
            else { f = vec2(d.x, -d.z) / a.y; col = 2.0; row = 1.0; }
        } else {
            if (d.z > 0.0) { f = vec2(d.x, d.y) / a.z; col = 1.0; row = 0.0; }
            else { f = vec2(-d.x, d.y) / a.z; col = 1.0; row = 1.0; }
        }
        // f = (a, b) da face; uv da face = (a, −b) em −1..1 → 0..1.
        const vec2 fu = vec2(f.x, -f.y) * 0.5 + 0.5;
        return (vec2(col, row) + clamp(fu, vec2(0.001), vec2(0.999))) / vec2(3.0, 2.0);
    }
    if (proj == 2) {
        const float th = acos(clamp(d.z, -1.0, 1.0));
        if (th > PI * 0.5 + 1e-4) { ok = false; return vec2(0.0); }
        const float r = th / (PI * 0.5);
        const vec2 dir2 = length(d.xy) > 1e-6 ? normalize(d.xy) : vec2(0.0);
        return vec2(dir2.x, -dir2.y) * r * 0.5 + 0.5;
    }
    if (d.z <= 1e-4) { ok = false; return vec2(0.0); }
    const float aspect = ae_layer_size().y / ae_layer_size().x;
    const vec2 p = d.xy / d.z / vec2(max(tanHalf, 1e-4), max(tanHalf * aspect, 1e-4));
    if (abs(p.x) > 1.0 || abs(p.y) > 1.0) { ok = false; return vec2(0.0); }
    return vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
}

void main() {
    const vec2 ls = ae_layer_size();
    const vec2 uvOut = ae_px() / ls;
    if (any(lessThan(uvOut, vec2(0.0))) || any(greaterThan(uvOut, vec2(1.0)))) { o_color = vec4(0.0); return; }
    bool ok;
    const vec3 dOut = to_dir(int(u.p[0].y + 0.5), uvOut, u.p[0].w, ok);
    if (!ok) { o_color = vec4(0.0); return; }
    const vec3 dIn = normalize(vec3(dot(u.p[1].xyz, dOut), dot(u.p[2].xyz, dOut), dot(u.p[3].xyz, dOut)));
    const vec2 uvIn = to_uv(int(u.p[0].x + 0.5), dIn, u.p[0].z, ok);
    if (!ok) { o_color = vec4(0.0); return; }
    // Equiretangular dá a volta: amostra a costura com o texel do outro lado.
    o_color = texture(u_tex0, ae_uv_of_px(uvIn * ls));
}

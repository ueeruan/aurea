#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_drizzle.frag
//
//  Garoa (o comportamento do CC Drizzle): gotas caem na camada como numa poça
//  e abrem anéis que se espalham e somem. Cada célula de lado "Raio da onda"
//  recebe uma gota por período (instante e lugar sorteados por célula e por
//  gota); o anel nunca passa do raio, então bastam a célula do pixel e as oito
//  vizinhas — custo fixo. Os anéis deslocam a imagem (refração) e acendem um
//  brilho especular fino nas cristas.
//
//  Tudo depende só do tempo do quadro: o mesmo quadro sai igual no preview,
//  no scrubbing e no export.
//
//  p0 = raio máximo (px), gotas por segundo, vida (s), anéis;
//  p1 = deslocamento (px), brilho, semente, tempo (s).
// =============================================================================
#include "cc_common.glsl"

void main() {
    const vec2 px = cc_px();
    const float Rmax = max(p.p0.x, 1.0);
    const float period = 1.0 / max(p.p0.y, 1e-3);
    const float life = max(p.p0.z, 1e-2);
    const float rings = max(p.p0.w, 1.0);
    const float wl = Rmax / max(rings * 1.5, 4.0);
    const uint seed = uint(p.p1.z) * 2246822519u;
    const vec2 home = floor(px / Rmax);
    vec2 push = vec2(0.0);
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const vec2 cell = home + vec2(float(i), float(j));
            const float offset = aurea_hash3(cell, seed + 11u);
            const float tt = p.p1.w / period - offset;
            const float n = floor(tt);
            const float age = (tt - n) * period;
            if (age > life) continue;
            const uint drop = seed + uint(int(n) & 0xFFFF) * 977u;
            const vec2 pos = (cell + vec2(aurea_hash3(cell, drop + 3u), aurea_hash3(cell, drop + 5u))) * Rmax;
            const vec2 d = px - pos;
            const float r = length(d);
            const float front = Rmax * (age / life);
            const float x = (front - r) / wl;   // anéis atrás da frente
            if (x <= 0.0 || x >= rings || r < 1e-3) continue;
            const float window = smoothstep(0.0, 0.35, x) * (1.0 - smoothstep(rings - 0.6, rings, x));
            const float fade = (1.0 - age / life);
            push += (d / r) * (cos(AUREA_TAU * x) * window * fade * fade);
        }
    }
    const vec4 col = cc_at(px - push * p.p1.x);
    const vec3 n = normalize(vec3(-push * 1.5, 1.0));
    const vec3 hv = normalize(vec3(-0.35, -0.55, 1.0) + vec3(0.0, 0.0, 1.0));
    const float spec = pow(max(dot(n, normalize(hv)), 0.0), 60.0) * max(p.p1.y, 0.0) * length(push);
    o_color = vec4(col.rgb + vec3(spec * col.a), col.a);
}

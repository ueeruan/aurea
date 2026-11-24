#version 450
// =============================================================================
//  Aurea / shaders / effects / particular.frag
//
//  Uma partícula do Particular. Disco (forma 0): borda suave; Suavidade 1 =
//  mancha que cai desde o centro, 0 = disco duro (o miolo para em 0,95 porque
//  smoothstep(1, 1, x) não é definido). Saída pré-multiplicada.
//
//  1.0.8: formas desenhadas por distância com sinal no quadrado -1..1 (y para
//  cima aqui): estrela, coração, folha, pétala, borboleta, confete, brilho e
//  floco de neve. A Suavidade vira o degradê da borda.
// =============================================================================
layout(location = 0) in vec2 v_corner;
layout(location = 1) in vec4 v_color;
layout(location = 2) flat in float v_feather;
layout(location = 3) flat in vec4 v_shape;
layout(location = 0) out vec4 o_color;
// Dentro do passe 3D (MRT): cor de exibição no alvo 0 e, no alvo 1, só o alfa que
// tapa a cena HDR atrás. Na folha 2D não há alvo 1 e a escrita é descartada.
layout(location = 1) out vec4 o_scene;

const float TWO_PI = 6.28318530718;

float sd_ellipse(vec2 q, vec2 r) { return (length(q / r) - 1.0) * min(r.x, r.y); }

float sd_segment(vec2 q, vec2 a, vec2 b) {
    const vec2 qa = q - a, ba = b - a;
    return length(qa - ba * clamp(dot(qa, ba) / dot(ba, ba), 0.0, 1.0));
}

vec2 turn(vec2 q, float a) {
    const float c = cos(a), s = sin(a);
    return vec2(c * q.x - s * q.y, s * q.x + c * q.y);
}

// Estrela de `n` pontas (raio 0,95, miolo `inner`), arestas retas: dobra o
// ângulo na metade de um setor e mede até a aresta ponta → vale.
float sd_star(vec2 q, float n, float inner) {
    const float sector = TWO_PI / n;
    const float a = abs(mod(atan(q.x, q.y) + sector * 0.5, sector) - sector * 0.5);
    const vec2 f = length(q) * vec2(cos(a), sin(a));
    const vec2 tip = vec2(0.95, 0.0);
    const vec2 valley = inner * vec2(cos(sector * 0.5), sin(sector * 0.5));
    const vec2 e = valley - tip, w = f - tip;
    const float side = e.x * w.y - e.y * w.x;
    return sd_segment(f, tip, valley) * (side > 0.0 ? -1.0 : 1.0);
}

// Coração implícito (x² + y² − 1)³ − x²y³, normalizado pelo gradiente para
// virar distância perto da borda.
float heart_f(vec2 q) {
    const vec2 h = vec2(q.x * 1.15, q.y * 1.12 + 0.118);
    const float a = dot(h, h) - 1.0;
    return a * a * a - h.x * h.x * h.y * h.y * h.y;
}
float sd_heart(vec2 q) {
    const float e = 0.004;
    const vec2 g = vec2(heart_f(q + vec2(e, 0.0)) - heart_f(q - vec2(e, 0.0)),
                        heart_f(q + vec2(0.0, e)) - heart_f(q - vec2(0.0, e))) / (2.0 * e);
    return heart_f(q) / max(length(g), 1e-3);
}

// Distância (< 0 dentro) e o tom do desenho: cor × tint, depois um tanto
// de branco (`white`).
float shape_sd(int s, vec2 q, out vec3 tint, out float white) {
    tint = vec3(1.0);
    white = 0.0;
    if (s == 1) return sd_star(q, 5.0, 0.42);
    if (s == 2) {
        const float d = sd_heart(q);
        white = 0.35 * (1.0 - smoothstep(0.0, 0.28, length(q - vec2(-0.42, 0.42))));   // brilho
        return d;
    }
    if (s == 3) {
        // Folha: lente de dois círculos, talo, nervura e veias.
        const vec2 l = q * 1.08;
        const float lens = max(length(l + vec2(0.5455, 0.0)) - 1.0955, length(l - vec2(0.5455, 0.0)) - 1.0955) / 1.08;
        const float stem = sd_segment(q, vec2(0.0, -0.8), vec2(0.06, -0.98)) - 0.035;
        const float rib = 1.0 - smoothstep(0.015, 0.045, abs(q.x));
        const float v = abs(fract((q.y - abs(q.x) * 0.8) * 5.0) - 0.5);
        const float vein = 1.0 - smoothstep(0.03, 0.08, v);
        tint = vec3((1.0 - 0.32 * rib) * (1.0 - 0.14 * vein) * mix(1.0, 0.84, smoothstep(-0.1, 0.3, q.x)));
        return min(lens, stem);
    }
    if (s == 4) {
        // Pétala de cerejeira: estreita na base, chanfro na ponta, base mais clara.
        vec2 w = q;
        w.x /= mix(0.35, 1.0, smoothstep(-1.0, 0.25, q.y));
        float d = sd_ellipse(w, vec2(0.62, 0.95));
        d = max(d, -(length(q - vec2(0.0, 0.98)) - 0.2));
        white = 0.55 * (1.0 - smoothstep(-0.95, 0.1, q.y));
        tint = vec3(mix(0.92, 1.0, smoothstep(0.2, 0.9, q.y)));
        return d;
    }
    if (s == 5) {
        // Borboleta: asas (de cima e de baixo) em elipses, corpo e antenas.
        const vec2 m = vec2(abs(q.x), q.y);
        const float up = sd_ellipse(turn(m - vec2(0.46, 0.27), -0.45), vec2(0.50, 0.36));
        const float lo = sd_ellipse(turn(m - vec2(0.31, -0.40), 0.6), vec2(0.34, 0.26));
        const float wings = min(up, lo);
        const float body = sd_segment(q, vec2(0.0, -0.62), vec2(0.0, 0.5)) - 0.07;
        const float ant = sd_segment(m, vec2(0.02, 0.48), vec2(0.2, 0.9)) - 0.022;
        const float rest = min(body, ant);
        if (rest < wings) {
            tint = vec3(0.16);
        } else {
            const float border = smoothstep(-0.13, -0.05, wings);
            tint = vec3(1.0 - 0.6 * border) * mix(1.0, 0.78, smoothstep(0.0, 0.25, -lo));
            white = 0.85 * (1.0 - smoothstep(0.05, 0.085, length(m - vec2(0.66, 0.44))));
        }
        return min(wings, rest);
    }
    if (s == 6) {
        // Confete: tirinha retangular.
        const vec2 b = vec2(0.9, 0.48);
        const vec2 o = abs(q) - b;
        return length(max(o, 0.0)) + min(max(o.x, o.y), 0.0);
    }
    if (s == 7) {
        // Brilho: estrela de 4 pontas finas.
        white = 0.6 * (1.0 - smoothstep(0.0, 0.3, length(q)));
        return sd_star(q, 4.0, 0.16);
    }
    // 8 Floco de neve: seis braços com galhos, dobrados num setor de 60°.
    const float sector = TWO_PI / 6.0;
    const float r = length(q);
    const float a = mod(atan(q.y, q.x) + sector * 0.5, sector) - sector * 0.5;
    const vec2 f = r * vec2(cos(a), abs(sin(a)));
    float d = sd_segment(f, vec2(0.0), vec2(0.9, 0.0));
    d = min(d, sd_segment(f, vec2(0.42, 0.0), vec2(0.64, 0.21)));
    d = min(d, sd_segment(f, vec2(0.64, 0.0), vec2(0.8, 0.14)));
    return min(d - 0.055, r - 0.15);
}

void main() {
    const int s = int(v_shape.x + 0.5);
    if (s <= 0) {
        const float d = length(v_corner);
        const float core = clamp(1.0 - v_feather, 0.0, 0.95);
        const float a = v_color.a * (1.0 - smoothstep(core, 1.0, d));
        if (a <= 0.0) discard;
        o_color = vec4(v_color.rgb * a, a);
        o_scene = vec4(0.0, 0.0, 0.0, a);
        return;
    }
    vec3 tint;
    float white;
    const vec2 q = vec2(v_corner.x, -v_corner.y);
    const float d = shape_sd(s, q, tint, white);
    // Borda: 1 px de antisserrilhado mais o degradê da Suavidade.
    const float aa = max(fwidth(d), 1e-4);
    float cover = clamp((aa * 0.5 - d) / (aa + v_feather * 0.45), 0.0, 1.0);
    cover = cover * cover * (3.0 - 2.0 * cover);
    const float a = v_color.a * cover;
    if (a <= 0.0) discard;
    const vec3 rgb = mix(clamp(v_color.rgb * tint, 0.0, 1.0), vec3(1.0), clamp(white, 0.0, 1.0));
    o_color = vec4(rgb * a, a);
    o_scene = vec4(0.0, 0.0, 0.0, a);
}

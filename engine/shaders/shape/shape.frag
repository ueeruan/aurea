#version 450
// =============================================================================
//  Aurea / shaders / shape / shape.frag
//
//  Forma vetorial por campo de distância (SDF), no espaço da layer (px, centro
//  na origem). Resolução independente: o mesmo contorno liso em qualquer
//  escala da layer — o renderer só escolhe quantos texels a textura tem.
//
//  Preenchimento e contorno com antisserrilhado de um texel; saída linear e
//  pré-multiplicada (o espaço de trabalho do compositor).
//
//  Tipos (batem com shape::Type em timeline/ShapeGeometry.hpp, que tem o
//  espelho desta conta na CPU — mudou aqui, muda lá):
//    0 retângulo (cantos arredondados)  1 elipse       3 polígono regular
//    4 estrela                          5 cruz         6 anel
//    7 fatia (pizza)                    8 flor         9 seta
//    10 triângulo retângulo             12 trapézio    13 paralelogramo
//    14 engrenagem                      15 seta dupla  16 linha
//    17 losango                         18 coração     19 selo
//    20 arco                            21 balão       22 raio
//    23 onda                            24 blob
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;      // xy = tamanho da layer (px), z = px por texel (antisserrilhado), w = tipo
    vec4 shape;     // x = raio do canto, y = pontas/lados, z = raio interno (0..1), w = preenchido (0/1)
    vec4 fill;      // linear, pré-multiplicado
    vec4 stroke;    // linear, pré-multiplicado
    vec4 extra;     // x = largura do contorno (px), y = profundidade, z = ponta, w = espessura
    vec4 more;      // x = abertura (graus), y = ponta da seta, z = haste, w = amplitude
    vec4 blob;      // xyz = fases do blob
} p;

const float PI = 3.14159265359;

float sd_round_box(vec2 q, vec2 b, float r) {
    r = min(r, min(b.x, b.y));
    vec2 d = abs(q) - b + r;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

float sd_ellipse(vec2 q, vec2 ab) {
    // Aproximação de primeira ordem (gradiente): exata no contorno, boa o
    // bastante para 1 texel de antisserrilhado.
    float k0 = length(q / ab);
    float k1 = length(q / (ab * ab));
    return k0 < 1e-6 ? -min(ab.x, ab.y) : k0 * (k0 - 1.0) / k1;
}

float sd_ngon(vec2 q, float r, float n) {
    float an = PI / n;
    float bn = mod(atan(q.x, -q.y), 2.0 * an) - an;   // um vértice para cima
    q = length(q) * vec2(cos(bn), abs(sin(bn)));
    q -= r * vec2(cos(an), sin(an));
    q.y += clamp(-q.y, 0.0, r * sin(an));
    return length(q) * sign(q.x);
}

float sd_star(vec2 q, float r, float n, float inner) {
    float an = PI / n;
    float a = mod(atan(q.x, -q.y), 2.0 * an) - an;
    q = length(q) * vec2(cos(a), abs(sin(a)));
    vec2 tip = vec2(r, 0.0);
    vec2 valley = inner * r * vec2(cos(an), sin(an));
    vec2 e = valley - tip;
    vec2 w = q - tip;
    float h = clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
    float d = length(w - e * h);
    float s = e.x * w.y - e.y * w.x;
    return s > 0.0 ? -d : d;
}

float sd_cross(vec2 q, vec2 b, float t) {
    q = abs(q);
    float a = sd_round_box(q, vec2(b.x, t), 0.0);
    float c = sd_round_box(q, vec2(t, b.y), 0.0);
    return min(a, c);
}

float sd_triangle(vec2 q, vec2 p0, vec2 p1, vec2 p2) {
    vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    vec2 v0 = q - p0, v1 = q - p1, v2 = q - p2;
    vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    float s = sign(e0.x * e2.y - e0.y * e2.x);
    vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                     vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                 vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}

// Trapézio isósceles: meia largura `top` em cima (y < 0), `bottom` embaixo.
float sd_trapezoid(vec2 q, float top, float bottom, float he) {
    vec2 k1 = vec2(bottom, he);
    vec2 k2 = vec2(bottom - top, 2.0 * he);
    q.x = abs(q.x);
    vec2 ca = vec2(q.x - min(q.x, q.y < 0.0 ? top : bottom), abs(q.y) - he);
    vec2 cb = q - k1 + k2 * clamp(dot(k1 - q, k2) / dot(k2, k2), 0.0, 1.0);
    float s = (cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0;
    return s * sqrt(min(dot(ca, ca), dot(cb, cb)));
}

// Paralelogramo (y para cima): base de meia largura `wi`, topo deslocado `sk`.
float sd_parallelogram(vec2 q, float wi, float he, float sk) {
    vec2 e = vec2(sk, he);
    q = q.y < 0.0 ? -q : q;
    vec2 w = q - e;
    w.x -= clamp(w.x, -wi, wi);
    vec2 d = vec2(dot(w, w), -w.y);
    float s = q.x * e.y - q.y * e.x;
    q = s < 0.0 ? -q : q;
    vec2 v = q - vec2(wi, 0.0);
    v -= e * clamp(dot(v, e) / dot(e, e), -1.0, 1.0);
    d = min(d, vec2(dot(v, v), wi * he - abs(s)));
    return sqrt(d.x) * sign(-d.y);
}

// Engrenagem: disco da raiz + `n` dentes retos + furo do cubo (fração da raiz).
// `depth` = altura do dente (fração do raio; 0,22 é a engrenagem de sempre).
float sd_gear(vec2 q, float r, float n, float hub, float depth) {
    float root = r * (1.0 - depth);
    float sector = 2.0 * PI / n;
    float a = mod(atan(q.x, -q.y) + sector * 0.5, sector) - sector * 0.5;
    float L = length(q);
    vec2 f = L * vec2(sin(a), cos(a));                 // y = raio, x = de lado
    float tw = root * sin(sector * 0.25);
    float tooth = sd_round_box(f - vec2(0.0, (root + r) * 0.5), vec2(tw, (r - root) * 0.5 + 1e-3), 0.0);
    float d = min(L - root, tooth);
    return max(d, hub * root - L);
}

// Polígono qualquer (par-ímpar), até 8 vértices.
float sd_polygon(vec2 q, vec2 v[8], int n) {
    float d = dot(q - v[0], q - v[0]);
    float s = 1.0;
    int j = n - 1;
    for (int i = 0; i < 8; ++i) {
        if (i >= n) break;
        vec2 e = v[j] - v[i];
        vec2 w = q - v[i];
        vec2 b = w - e * clamp(dot(w, e) / dot(e, e), 0.0, 1.0);
        d = min(d, dot(b, b));
        bool c0 = q.y >= v[i].y;
        bool c1 = q.y < v[j].y;
        bool c2 = e.x * w.y > e.y * w.x;
        if ((c0 && c1 && c2) || (!c0 && !c1 && !c2)) s = -s;
        j = i;
    }
    return s * sqrt(d);
}

// Setor que começa no leste e gira no sentido horário da tela por `sweepDeg`.
float sd_wedge(vec2 q, float sweepDeg) {
    float s = sweepDeg * PI / 180.0;
    float h1 = -q.y;
    float h2 = q.y * cos(s) - q.x * sin(s);
    return s <= PI ? max(h1, h2) : min(h1, h2);
}

// Coração: lóbulos circulares encostados no topo e nos lados + o polígono das
// tangentes até a ponta. `depth` = entalhe de cima.
float sd_heart(vec2 pp, float m, float depth) {
    vec2 u = pp / m;
    float dl = clamp(depth, 0.05, 0.95) * 0.5;
    float R = (1.0 + dl) - sqrt(2.0 * dl);
    vec2 C = vec2(1.0 - R, R - 1.0);
    vec2 P = vec2(0.0, 1.0);
    vec2 v = P - C;
    float L = length(v);
    vec2 vh = v / L;
    float ca = R / L;
    float sa = sqrt(max(0.0, 1.0 - ca * ca));
    vec2 T = C + (vh * ca + vec2(vh.y, -vh.x) * sa) * R;
    float lobe = length(vec2(abs(u.x), u.y) - C) - R;
    vec2 poly[8];
    poly[0] = P; poly[1] = T; poly[2] = C; poly[3] = vec2(-C.x, C.y); poly[4] = vec2(-T.x, T.y);
    poly[5] = P; poly[6] = P; poly[7] = P;
    return min(lobe, sd_polygon(u, poly, 5)) * m;
}

// Selo: círculo com saliências em arco (uma em cima), cada uma um disco que
// passa pelos dois vales.
float sd_seal(vec2 pp, float m, float count, float depth) {
    float n = clamp(floor(count + 0.5), 3.0, 64.0);
    float h = PI / n;
    float dmax = 1.0 - 1.0 / (cos(h) + sin(h));
    float dl = 0.01 + clamp(depth, 0.0, 1.0) * max(0.0, dmax - 0.01);
    float v = 1.0 - dl;
    float c = (1.0 - v * v) / (2.0 * (1.0 - v * cos(h)));
    float b = 1.0 - c;
    vec2 u = pp / m;
    float L = length(u);
    float a = mod(atan(u.x, -u.y) + h, 2.0 * h) - h;
    vec2 f = vec2(L * abs(sin(a)), L * cos(a));
    float bump = length(f - vec2(0.0, c)) - b;
    float core = f.y - v * cos(h);
    return min(bump, core) * m;
}

// Distância até y = amp·sen(k·(x + hx)): busca no período + 3 passos de Newton.
float wave_distance(vec2 q, float hx, float amp, float k) {
    if (amp < 1e-3) return abs(q.y);
    float lam = 2.0 * PI / k;
    float best = 1e30;
    float bt = q.x;
    for (int i = 0; i <= 8; ++i) {
        float t = q.x + (float(i) / 8.0 - 0.5) * lam;
        float dx = t - q.x;
        float dy = amp * sin(k * (t + hx)) - q.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; bt = t; }
    }
    for (int j = 0; j < 3; ++j) {
        float ph = k * (bt + hx);
        float sv = amp * sin(ph);
        float s1 = amp * k * cos(ph);
        float s2 = -amp * k * k * sin(ph);
        float g1 = (bt - q.x) + (sv - q.y) * s1;
        float g2 = 1.0 + s1 * s1 + (sv - q.y) * s2;
        if (g2 > 1e-4) bt -= g1 / g2;
    }
    float dx = bt - q.x;
    float dy = amp * sin(k * (bt + hx)) - q.y;
    return sqrt(min(best, dx * dx + dy * dy));
}

// Blob: raio polar com harmônicos n, n+1 e 2 (fases da variante, vindas da CPU).
float sd_blob(vec2 pp, float m, float count, float variation, vec3 ph) {
    float n = clamp(floor(count + 0.5), 2.0, 16.0);
    float a = clamp(variation, 0.05, 0.95) * 0.35;
    float R = m / (1.0 + a);
    float L = length(pp);
    float th = atan(pp.y, pp.x);
    float t1 = n * th + ph.x;
    float t2 = (n + 1.0) * th + ph.y;
    float t3 = 2.0 * th + ph.z;
    float r = 1.0 + a * (0.55 * cos(t1) + 0.30 * cos(t2) + 0.15 * cos(t3));
    float dr = -a * (0.55 * n * sin(t1) + 0.30 * (n + 1.0) * sin(t2) + 0.30 * sin(t3));
    float g = R * dr / max(L, 1e-3 * m);
    return (L - R * r) / sqrt(1.0 + g * g);
}

// Balão de fala: corpo arredondado (76% da altura) + rabicho até a base.
float sd_bubble(vec2 q, vec2 half_, float corner, float tip) {
    float bodyH = half_.y * 2.0 * 0.76;
    float bottom = -half_.y + bodyH;
    vec2 bh = vec2(half_.x, bodyH * 0.5);
    float r = clamp(corner, 0.0, min(bh.x, bh.y));
    float body = sd_round_box(q - vec2(0.0, -half_.y + bh.y), bh, r);
    float tx = clamp(half_.x * (2.0 * clamp(tip, 0.0, 1.0) - 1.0), -half_.x * 0.92, half_.x * 0.92);
    float bw = half_.x * 0.16;
    float lo = -half_.x + r + bw;
    float hi = half_.x - r - bw;
    float xb = lo < hi ? clamp(tx * 0.55, lo, hi) : 0.0;
    float yb = bottom - min(bodyH * 0.25, max(r, 2.0));
    float tail = sd_triangle(q, vec2(xb - bw, yb), vec2(tx, half_.y), vec2(xb + bw, yb));
    return min(body, tail);
}

// Raio (relâmpago): 7 vértices na caixa [-1, 1]²; `tip` inclina (0,5 = reto).
float sd_bolt(vec2 q, vec2 half_, float tip) {
    float k = (clamp(tip, 0.0, 1.0) - 0.5) * 0.8;
    vec2 v[8];
    v[0] = vec2(-0.05, -1.0); v[1] = vec2(0.60, -1.0); v[2] = vec2(0.12, -0.12); v[3] = vec2(0.70, -0.12);
    v[4] = vec2(-0.42, 1.0); v[5] = vec2(-0.02, 0.14); v[6] = vec2(-0.62, 0.14); v[7] = v[6];
    for (int i = 0; i < 7; ++i) v[i] = vec2((v[i].x - k * v[i].y) * half_.x, v[i].y * half_.y);
    return sd_polygon(q, v, 7);
}

float shape_sd(vec2 q, vec2 half_) {
    int type = int(p.size.w + 0.5);
    float m = min(half_.x, half_.y);
    // Formas "redondas" num retângulo não quadrado: esticadas pela proporção.
    vec2 st = half_ / m;
    float mst = min(st.x, st.y);
    float depth = p.extra.y;
    float tip = p.extra.z;
    float thick = p.extra.w;
    float sweep = p.more.x;
    float head = p.more.y;
    float shaft = p.more.z;
    float amp = p.more.w;
    if (type == 0) return sd_round_box(q, half_, p.shape.x);
    if (type == 1) return sd_ellipse(q, half_);
    if (type == 3) return sd_ngon(q / st, m, max(3.0, p.shape.y)) * mst;
    if (type == 4) return sd_star(q / st, m, max(3.0, p.shape.y), clamp(p.shape.z, 0.05, 0.95)) * mst;
    if (type == 5) return sd_cross(q, half_, m * clamp(p.shape.z, 0.05, 0.95));
    if (type == 6) {
        float ring = m * (1.0 - clamp(p.shape.z, 0.05, 0.95)) * 0.5;
        return abs(sd_ellipse(q, half_ - ring)) - ring;
    }
    if (type == 7) {
        // Fatia: elipse recortada pelo setor que sai do leste no sentido
        // horário (270° = a pizza de 3/4 de sempre, sem o quadrante de cima à direita).
        float e = sd_ellipse(q, half_);
        return sweep >= 359.9 ? e : max(e, sd_wedge(q, sweep));
    }
    if (type == 8) {
        float n = max(3.0, p.shape.y);
        float a = atan(q.y, q.x);
        float r = m * ((1.0 - depth) + depth * cos(n * a));
        return (length(q / st) - r) * mst * 0.8;
    }
    if (type == 9) {
        // Seta para a direita: haste + ponta triangular (a haste entra 4/9 na ponta).
        float s = half_.x * (1.0 - 2.0 * head);
        float end = s + (half_.x - s) * (4.0 / 9.0);
        float body = sd_round_box(q - vec2((end - half_.x) * 0.5, 0.0), vec2((end + half_.x) * 0.5, half_.y * shaft * 0.5), 0.0);
        float point = sd_triangle(q, vec2(s, -half_.y), vec2(half_.x, 0.0), vec2(s, half_.y));
        return min(body, point);
    }
    if (type == 10) return sd_triangle(q, vec2(-half_.x, -half_.y), vec2(-half_.x, half_.y), vec2(half_.x, half_.y));
    if (type == 12) return sd_trapezoid(q, half_.x * clamp(p.shape.z, 0.05, 0.95), half_.x, half_.y);
    if (type == 13) {
        float sk = half_.x * clamp(p.shape.z, 0.05, 0.95) * 0.5;
        return sd_parallelogram(vec2(q.x, -q.y), half_.x - sk, half_.y, sk);
    }
    if (type == 14) return sd_gear(q / st, m, max(3.0, p.shape.y), clamp(p.shape.z, 0.05, 0.95), depth) * mst;
    if (type == 15) {
        // Seta dupla: haste de espessura `z` e uma ponta em cada lado.
        float t = half_.y * clamp(p.shape.z, 0.05, 0.95);
        float s = half_.x * (1.0 - 2.0 * head);
        float end = s + (half_.x - s) * (3.0 / 11.0);
        float body = sd_round_box(q, vec2(end, t), 0.0);
        float right = sd_triangle(q, vec2(s, -half_.y), vec2(half_.x, 0.0), vec2(s, half_.y));
        float left = sd_triangle(q, vec2(-s, half_.y), vec2(-half_.x, 0.0), vec2(-s, -half_.y));
        return min(body, min(left, right));
    }
    if (type == 16) return sd_round_box(q, vec2(half_.x, max(0.5, half_.y * thick)), p.shape.x);
    if (type == 17) {
        float yw = half_.y * (2.0 * clamp(tip, 0.05, 0.95) - 1.0);
        vec2 v[8];
        v[0] = vec2(0.0, -half_.y); v[1] = vec2(half_.x, yw); v[2] = vec2(0.0, half_.y); v[3] = vec2(-half_.x, yw);
        v[4] = v[3]; v[5] = v[3]; v[6] = v[3]; v[7] = v[3];
        return sd_polygon(q, v, 4);
    }
    if (type == 18) return sd_heart(q / st, m, depth) * mst;
    if (type == 19) return sd_seal(q / st, m, p.shape.y, depth) * mst;
    if (type == 20) {
        float T = clamp(thick, 0.02, 0.5) * 2.0 * m;
        vec2 qs = q / st;
        float ring = abs(length(qs) - (m - T * 0.5)) - T * 0.5;
        return (sweep >= 359.9 ? ring : max(ring, sd_wedge(qs, sweep))) * mst;
    }
    if (type == 21) return sd_bubble(q, half_, p.shape.x, tip);
    if (type == 22) return sd_bolt(q, half_, tip);
    if (type == 23) {
        float n = clamp(floor(p.shape.y + 0.5), 1.0, 32.0);
        float T = clamp(thick, 0.02, 1.0) * 2.0 * half_.y;
        float a = clamp(amp, 0.0, 1.0) * max(0.0, half_.y - T * 0.5);
        float k = PI * n / max(half_.x, 1e-3);
        return max(wave_distance(q, half_.x, a, k) - T * 0.5, abs(q.x) - half_.x);
    }
    if (type == 24) return sd_blob(q / st, m, p.shape.y, depth, p.blob.xyz) * mst;
    return sd_round_box(q, half_, 0.0);
}

void main() {
    vec2 half_ = p.size.xy * 0.5;
    vec2 q = (v_uv - 0.5) * p.size.xy;
    float aa = max(p.size.z, 1e-3);
    float d = shape_sd(q, half_ - vec2(p.extra.x * 0.5));
    vec4 c = vec4(0.0);
    if (p.shape.w > 0.5) {
        float cov = clamp(0.5 - d / aa, 0.0, 1.0);
        c = p.fill * cov;
    }
    if (p.extra.x > 0.0) {
        float sd = abs(d) - p.extra.x * 0.5;
        float cov = clamp(0.5 - sd / aa, 0.0, 1.0);
        // Contorno por cima do preenchimento ("over" pré-multiplicado).
        c = p.stroke * cov + c * (1.0 - p.stroke.a * cov);
    }
    o_color = c;
}

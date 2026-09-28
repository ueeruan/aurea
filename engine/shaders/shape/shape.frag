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
//  Tipos (batem com ShapeType em timeline/Layer.hpp):
//    0 retângulo (cantos arredondados)  1 elipse       3 polígono regular
//    4 estrela                          5 cruz         6 anel
//    7 fatia (pizza)                    8 flor         9 seta
//    10 triângulo retângulo             12 trapézio    13 paralelogramo
//    14 engrenagem                      15 seta dupla
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 size;      // xy = tamanho da layer (px), z = px por texel (antisserrilhado), w = tipo
    vec4 shape;     // x = raio do canto, y = pontas/lados, z = raio interno (0..1), w = preenchido (0/1)
    vec4 fill;      // linear, pré-multiplicado
    vec4 stroke;    // linear, pré-multiplicado
    vec4 extra;     // x = largura do contorno (px)
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

float sd_segment(vec2 q, vec2 a, vec2 b) {
    vec2 pa = q - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
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
float sd_gear(vec2 q, float r, float n, float hub) {
    float root = r * 0.78;
    float sector = 2.0 * PI / n;
    float a = mod(atan(q.x, -q.y) + sector * 0.5, sector) - sector * 0.5;
    float L = length(q);
    vec2 f = L * vec2(sin(a), cos(a));                 // y = raio, x = de lado
    float tw = root * sin(sector * 0.25);
    float tooth = sd_round_box(f - vec2(0.0, (root + r) * 0.5), vec2(tw, (r - root) * 0.5 + 1e-3), 0.0);
    float d = min(L - root, tooth);
    return max(d, hub * root - L);
}

float shape_sd(vec2 q, vec2 half_) {
    int type = int(p.size.w + 0.5);
    float m = min(half_.x, half_.y);
    // Formas "redondas" num retângulo não quadrado: esticadas pela proporção.
    vec2 st = half_ / m;
    if (type == 0) return sd_round_box(q, half_, p.shape.x);
    if (type == 1) return sd_ellipse(q, half_);
    if (type == 3) return sd_ngon(q / st, m, max(3.0, p.shape.y)) * min(st.x, st.y);
    if (type == 4) return sd_star(q / st, m, max(3.0, p.shape.y), clamp(p.shape.z, 0.05, 0.95)) * min(st.x, st.y);
    if (type == 5) return sd_cross(q, half_, m * clamp(p.shape.z, 0.05, 0.95));
    if (type == 6) {
        float ring = m * (1.0 - clamp(p.shape.z, 0.05, 0.95)) * 0.5;
        return abs(sd_ellipse(q, half_ - ring)) - ring;
    }
    if (type == 7) {
        // Fatia de 3/4 (a pizza clássica): elipse menos o quadrante de cima à direita.
        // Quadrante (x > 0, y < 0): campo negativo dentro dele; subtraído da elipse.
        float e = sd_ellipse(q, half_);
        float quadrant = max(-q.x, q.y);
        return max(e, -quadrant);
    }
    if (type == 8) {
        float n = max(3.0, p.shape.y);
        float a = atan(q.y, q.x);
        float r = m * (0.72 + 0.28 * cos(n * a));
        return (length(q / st) - r) * min(st.x, st.y) * 0.8;
    }
    if (type == 9) {
        // Seta para a direita: haste + ponta triangular.
        float shaft = sd_round_box(q - vec2(-half_.x * 0.25, 0.0), vec2(half_.x * 0.75, half_.y * 0.22), 0.0);
        float head = sd_triangle(q, vec2(half_.x * 0.1, -half_.y), vec2(half_.x, 0.0), vec2(half_.x * 0.1, half_.y));
        return min(shaft, head);
    }
    if (type == 10) return sd_triangle(q, vec2(-half_.x, -half_.y), vec2(-half_.x, half_.y), vec2(half_.x, half_.y));
    if (type == 12) return sd_trapezoid(q, half_.x * clamp(p.shape.z, 0.05, 0.95), half_.x, half_.y);
    if (type == 13) {
        float sk = half_.x * clamp(p.shape.z, 0.05, 0.95) * 0.5;
        return sd_parallelogram(vec2(q.x, -q.y), half_.x - sk, half_.y, sk);
    }
    if (type == 14) return sd_gear(q / st, m, max(3.0, p.shape.y), clamp(p.shape.z, 0.05, 0.95)) * min(st.x, st.y);
    if (type == 15) {
        // Seta dupla: haste de espessura `z` e uma ponta em cada lado.
        float t = half_.y * clamp(p.shape.z, 0.05, 0.95);
        float shaft = sd_round_box(q, vec2(half_.x * 0.6, t), 0.0);
        float right = sd_triangle(q, vec2(half_.x * 0.45, -half_.y), vec2(half_.x, 0.0), vec2(half_.x * 0.45, half_.y));
        float left = sd_triangle(q, vec2(-half_.x * 0.45, half_.y), vec2(-half_.x, 0.0), vec2(-half_.x * 0.45, -half_.y));
        return min(shaft, min(left, right));
    }
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

// =============================================================================
//  Aurea / shaders / particles / particles_extras.glsl  —  AUREA PARTICULAR 8.2
//
//  O que não cabe no bloco de uniformes: vem do STORAGE BUFFER da camada
//  (binding AUREA_DATA), montado em render/ParticleExtras.cpp. Layout do
//  cabeçalho em ParticleExtras.hpp (H0..H34); o cabeçalho do quadro começa em
//  `p.extras.y` e só vale com `p.extras.z` = 1.
//
//  Tudo aqui continua FECHADO: nenhuma função guarda estado. Os pontos de
//  emissão são uma distribuição da fonte; quem escolhe o ponto é o hash da
//  partícula (semente, slot, geração).
//
//  Incluído DEPOIS do bloco `p` (lê p.extras e p.collide).
// =============================================================================
#ifndef AUREA_PARTICLES_EXTRAS_GLSL
#define AUREA_PARTICLES_EXTRAS_GLSL

layout(set = 0, binding = AUREA_DATA, std430) readonly buffer ParticularData {
    vec4 d[];
} u_px;

const int kFlagSourcePoints = 1;
const int kFlagTexture = 2;
const int kFlagMesh = 4;

bool px_on() { return p.extras.z > 0.5; }
vec4 px_h(int i) { return u_px.d[int(p.extras.y + 0.5) + i]; }
int px_flags() { return px_on() ? int(px_h(1).z + 0.5) : 0; }
bool px_texture() { return (px_flags() & kFlagTexture) != 0; }
/// Desenho de malha (o passe decide pelo pipeline; o shader pela bandeira).
bool px_mesh() { return p.extras.w > 0.5 && (px_flags() & kFlagMesh) != 0; }

/// Nascimento num ponto da FONTE (camada, texto, caminho, malha). Falso = sem
/// fonte pronta: quem chama cai na forma do emissor (caixa).
bool emit_from_source(inout uint s, out vec3 start) {
    start = vec3(0.0);
    if ((px_flags() & kFlagSourcePoints) == 0) return false;
    const vec4 h0 = px_h(0);
    const int n = int(h0.x + 0.5);
    if (n <= 0) return false;
    const int i = min(int(rnd(s) * float(n)), n - 1);
    vec3 q = u_px.d[int(h0.y + 0.5) + i].xyz;
    // Célula: o ponto cai em qualquer lugar dela (a superfície fica contínua,
    // não uma grade de pontos).
    if (h0.z > 0.5) q.xy += (vec2(rnd(s), rnd(s)) - 0.5) * h0.w;
    // px da camada ← fonte (projetiva: fonte no 3D sai pela câmera).
    const mat4 M = mat4(px_h(6), px_h(7), px_h(8), px_h(9));
    const vec4 c = M * vec4(q, 1.0);
    start = vec3(c.xy / (abs(c.w) > 1e-6 ? c.w : 1.0), 0.0);
    return true;
}

/// Curva suave por pontos (posição crescente, até 8): Hermite com tangentes de
/// Catmull-Rom, presa entre os dois vizinhos (sem passar do valor pedido).
float px_curve(int first, int n, float u) {
    vec4 a = px_h(first);
    if (n == 1 || u <= a.x) return a.y;
    for (int i = 1; i < 8; ++i) {
        if (i >= n) break;
        const vec4 b = px_h(first + i);
        if (u <= b.x) {
            const float dx = max(b.x - a.x, 1e-5);
            const float t = clamp((u - a.x) / dx, 0.0, 1.0);
            const vec4 a0 = px_h(first + max(i - 2, 0));
            const vec4 b1 = px_h(first + min(i + 1, n - 1));
            const float m0 = (b.y - a0.y) / max(b.x - a0.x, 1e-5) * dx;
            const float m1 = (b1.y - a.y) / max(b1.x - a.x, 1e-5) * dx;
            const float t2 = t * t, t3 = t2 * t;
            const float v = (2.0 * t3 - 3.0 * t2 + 1.0) * a.y + (t3 - 2.0 * t2 + t) * m0
                          + (-2.0 * t3 + 3.0 * t2) * b.y + (t3 - t2) * m1;
            return clamp(v, min(a.y, b.y), max(a.y, b.y));
        }
        a = b;
    }
    return a.y;
}

/// Cor ao longo da vida: gradiente de até 8 paradas; sem paradas, início → fim.
vec3 life_color(float u, vec3 startCol, vec3 endCol) {
    const int n = px_on() ? int(px_h(10).x + 0.5) : 0;
    if (n <= 0) return mix(startCol, endCol, u);
    vec4 a = px_h(11);
    if (u <= a.x) return a.yzw;
    for (int i = 1; i < 8; ++i) {
        if (i >= n) break;
        const vec4 b = px_h(11 + i);
        if (u <= b.x) return mix(a.yzw, b.yzw, clamp((u - a.x) / max(b.x - a.x, 1e-5), 0.0, 1.0));
        a = b;
    }
    return a.yzw;
}

/// Tamanho ao longo da vida: com curva, tamanho inicial × curva(u).
float life_size(float u, float s0, float s1) {
    const int n = px_on() ? int(px_h(10).y + 0.5) : 0;
    return n <= 0 ? mix(s0, s1, u) : s0 * px_curve(19, n, u);
}

/// Opacidade ao longo da vida: com curva, opacidade inicial × curva(u).
float life_opacity(float u, float o0, float o1) {
    const int n = px_on() ? int(px_h(10).z + 0.5) : 0;
    return n <= 0 ? mix(o0, o1, u) : o0 * px_curve(27, n, u);
}

/// Aleatórios por partícula (tamanho, opacidade, cor), do fluxo `s2` — um
/// fluxo SEPARADO do da física: ligar um aleatório não mexe na trajetória.
void random_look(inout uint s2, out float sizeMul, out float opMul, out vec3 tint) {
    const vec4 r = px_on() ? px_h(2) : vec4(0.0);
    sizeMul = max(0.0, 1.0 + (rnd(s2) - 0.5) * 2.0 * r.x);
    opMul = clamp(1.0 - rnd(s2) * r.y, 0.0, 1.0);
    tint = max(vec3(0.0), vec3(1.0) + (vec3(rnd(s2), rnd(s2), rnd(s2)) - 0.5) * 2.0 * r.z);
}

/// Probabilidade de uma primária gerar as secundárias.
float aux_probability() { return px_on() ? px_h(2).w : 1.0; }
/// Largura (× tamanho) e opacidade da cauda do rastro.
vec2 trail_look() { return px_on() ? px_h(3).xy : vec2(1.0); }

// --- Colisão esfera/caixa (campo de distância assinada) -----------------------
// Modo 2 = esfera (centro H4.xyz, raio H4.w); 3 = caixa (meia-caixa H5.xyz).
float col_sdf(vec3 q) {
    const vec4 c = px_h(4);
    if (p.collide.x < 2.5) return length(q - c.xyz) - c.w;
    const vec3 dd = abs(q - c.xyz) - px_h(5).xyz;
    return length(max(dd, 0.0)) + min(max(dd.x, max(dd.y, dd.z)), 0.0);
}
vec3 col_normal(vec3 q) {
    const float e = 0.25;
    const vec3 g = vec3(col_sdf(q + vec3(e, 0, 0)) - col_sdf(q - vec3(e, 0, 0)),
                        col_sdf(q + vec3(0, e, 0)) - col_sdf(q - vec3(0, e, 0)),
                        col_sdf(q + vec3(0, 0, e)) - col_sdf(q - vec3(0, 0, e)));
    const float l = length(g);
    return l > 1e-6 ? g / l : vec3(0.0, -1.0, 0.0);
}
bool col_volume() { return px_on() && p.collide.x > 1.5 && p.collide.x < 3.5; }
float col_bounce() { return px_h(5).w; }

// --- Malha instanciada -------------------------------------------------------
void mesh_vertex(int vi, out vec3 pos, out vec3 nrm, out vec4 color) {
    const int base = int(px_h(1).y + 0.5) + vi * 2;
    const vec4 a = u_px.d[base];
    const vec4 b = u_px.d[base + 1];
    pos = a.xyz;
    nrm = b.xyz;
    color = unpackUnorm4x8(floatBitsToUint(b.w));
}
float mesh_scale() { return px_h(3).z; }
bool mesh_lit() { return px_h(3).w > 0.5; }

/// Rotação de Rodrigues (eixo unitário, ângulo em rad).
vec3 rotate_axis(vec3 v, vec3 k, float a) {
    const float c = cos(a), s = sin(a);
    return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}

#endif

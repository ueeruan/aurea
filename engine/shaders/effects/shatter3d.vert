#version 450
// =============================================================================
//  Aurea / shaders / effects / shatter3d.vert
//
//  3D Shatter: a camada vira peças com espessura (quadrados, triângulos ou
//  cacos de vidro) que explodem em 3D a partir do centro da força. 36 vértices
//  por peça, sem vertex buffer: frente (2 triângulos), verso (2) e as laterais
//  (uma por aresta). Peça parada desenha só a frente, no lugar exato da
//  camada (mesmo quadro sem o efeito); a peça solta gira em torno do próprio
//  centro, voa, cai pela gravidade e é vista por uma câmera em perspectiva.
//  Implementação própria; a referência de comportamento é o "Shatter" dos
//  editores de movimento.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 src;      // região da camada (px): x, y, largura, altura
    vec4 dst;      // região da saída (px)
    vec4 grid;     // colunas, linhas, forma (0 quadrado, 1 triângulo, 2 vidro), semente
    vec4 motion;   // explosão (0..1), força (px), giro (rad), gravidade (px)
    vec4 shape;    // espessura (px), para a câmera (−1..1), aleatoriedade (0..1), onda (0..1)
    vec4 view;     // centro da força (px) xy, distância da câmera (px), luz (0..1)
    vec4 misc;     // esmaecer (0..1), largura e altura da célula (px), maior distância ao centro (px)
} p;

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out vec2 v_info;   // sombra da face, opacidade

float hash1(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return float(x & 0xFFFFFFu) / 16777216.0;
}

uint cell_hash(ivec2 c, uint salt) {
    return (uint(c.x) * 73856093u) ^ (uint(c.y) * 19349663u) ^ (uint(p.grid.w) * 83492791u + salt);
}

// Vértice da grade (px). No vidro os vértices de dentro tremem — o mesmo
// tremor para as peças vizinhas, então nada abre fresta com a camada parada.
vec2 grid_point(ivec2 g) {
    vec2 at = p.src.xy + vec2(g) * p.misc.yz;
    const int cols = max(int(p.grid.x + 0.5), 1);
    const int rows = max(int(p.grid.y + 0.5), 1);
    if (g.x >= cols) at.x = p.src.x + p.src.z;
    if (g.y >= rows) at.y = p.src.y + p.src.w;
    if (p.grid.z > 1.5 && g.x > 0 && g.y > 0 && g.x < cols && g.y < rows) {
        const vec2 j = vec2(hash1(cell_hash(g, 11u)), hash1(cell_hash(g, 29u))) - 0.5;
        at += j * p.misc.yz * 0.7;
    }
    return at;
}

mat3 rotation(vec3 axis, float angle) {
    const float c = cos(angle), s = sin(angle), t = 1.0 - c;
    const vec3 a = axis;
    return mat3(t * a.x * a.x + c,       t * a.x * a.y + s * a.z, t * a.x * a.z - s * a.y,
                t * a.x * a.y - s * a.z, t * a.y * a.y + c,       t * a.y * a.z + s * a.x,
                t * a.x * a.z + s * a.y, t * a.y * a.z - s * a.x, t * a.z * a.z + c);
}

void main() {
    v_uv = vec2(0.0);
    v_info = vec2(0.0);
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   // vértice descartado: fora do recorte

    const uint vid = uint(gl_VertexIndex);
    const uint piece = vid / 36u;
    const uint k = vid % 36u;
    const int cols = max(int(p.grid.x + 0.5), 1);
    const int rows = max(int(p.grid.y + 0.5), 1);
    const bool triangles = p.grid.z > 0.5;
    const int cellIndex = triangles ? int(piece / 2u) : int(piece);
    const ivec2 cell = ivec2(cellIndex % cols, cellIndex / cols);
    if (cell.y >= rows) return;

    const vec2 A = grid_point(cell), B = grid_point(cell + ivec2(1, 0));
    const vec2 C = grid_point(cell + ivec2(1, 1)), D = grid_point(cell + ivec2(0, 1));
    vec2 c[4];
    int n = 4;
    if (!triangles) {
        c[0] = A; c[1] = B; c[2] = C; c[3] = D;
    } else {
        n = 3;
        const bool diag = hash1(cell_hash(cell, 7u)) > 0.5;
        const bool second = (piece & 1u) == 1u;
        if (diag) {
            if (!second) { c[0] = A; c[1] = B; c[2] = C; } else { c[0] = A; c[1] = C; c[2] = D; }
        } else {
            if (!second) { c[0] = A; c[1] = B; c[2] = D; } else { c[0] = B; c[1] = C; c[2] = D; }
        }
        c[3] = c[2];
    }
    const vec2 middle = n == 4 ? (c[0] + c[1] + c[2] + c[3]) * 0.25 : (c[0] + c[1] + c[2]) / 3.0;

    // Instante em que a peça se solta: perto do centro primeiro (onda),
    // misturado a um sorteio pela aleatoriedade. Na explosão 100% todas já
    // fizeram o voo inteiro.
    const uint id = cell_hash(cell, 3u) * 2u + (piece & 1u);
    const float h0 = hash1(id * 747796405u + 1u);
    const vec2 toPiece = middle - p.view.xy;
    const float dist = length(toPiece);
    const float near = clamp(dist / max(p.misc.w, 1e-3), 0.0, 1.0);
    const float release = p.shape.w * mix(near, h0, p.shape.z * 0.6) * 0.6;
    const float age = clamp((p.motion.x - release) / max(1.0 - release, 1e-3), 0.0, 1.0);
    const float thick = max(p.shape.x, 0.0);
    const bool solid = age > 0.0 && thick >= 0.5;

    int corner = 0;
    float z = 0.0;
    int face = 0;          // 0 frente, 1 verso, 2 lateral
    vec3 nrm = vec3(0.0, 0.0, 1.0);
    if (k < 6u) {
        const uint t = k / 3u, j = k % 3u;
        if (n == 3 && t == 1u) return;
        corner = j == 0u ? 0 : int(t + j);
    } else if (k < 12u) {
        if (!solid) return;
        const uint t = (k - 6u) / 3u, j = (k - 6u) % 3u;
        if (n == 3 && t == 1u) return;
        corner = j == 0u ? 0 : int(t + (j == 1u ? 2u : 1u));
        z = -thick;
        face = 1;
        nrm = vec3(0.0, 0.0, -1.0);
    } else {
        if (!solid) return;
        const uint s = (k - 12u) / 6u, j = (k - 12u) % 6u;
        if (int(s) >= n) return;
        const int a = int(s), b = (int(s) + 1) % n;
        const bool useB = j == 1u || j == 2u || j == 4u;
        const bool back = j == 2u || j == 4u || j == 5u;
        corner = useB ? b : a;
        z = back ? -thick : 0.0;
        face = 2;
        const vec2 e = c[b] - c[a];
        vec2 nn = normalize(vec2(e.y, -e.x) + vec2(1e-6));
        if (dot(nn, (c[a] + c[b]) * 0.5 - middle) < 0.0) nn = -nn;
        nrm = vec3(nn, 0.0);
    }

    const vec2 at = c[corner];
    vec3 P = vec3(at, z);
    vec3 N = nrm;
    if (age > 0.0) {
        const float h1 = hash1(id * 2891336453u + 2u);
        const float h2 = hash1(id * 1597334677u + 3u);
        const float h3 = hash1(id * 3266489917u + 4u);
        const float h4 = hash1(id * 668265263u + 5u);
        const float h5 = hash1(id * 374761393u + 6u);
        const float h6 = hash1(id * 2246822519u + 7u);
        const float h7 = hash1(id * 3266489917u + 8u);
        const float h8 = hash1(id * 1103515245u + 9u);
        vec2 dir2 = dist > 1e-3 ? toPiece / dist : vec2(cos(h1 * 6.2831853), sin(h1 * 6.2831853));
        const float jitter = (h2 - 0.5) * p.shape.z * 1.2;
        const float cj = cos(jitter), sj = sin(jitter);
        dir2 = vec2(dir2.x * cj - dir2.y * sj, dir2.x * sj + dir2.y * cj);
        const vec3 dir = normalize(vec3(dir2, p.shape.y * 1.5 + (h3 - 0.5) * p.shape.z));
        const float strength = p.motion.y * (0.6 + 0.8 * h4) * (1.25 - 0.5 * near);
        const vec3 disp = dir * strength * age + vec3(0.0, p.motion.w * age * age, 0.0);
        const vec3 axis = normalize(vec3(h5 - 0.5, h6 - 0.5, h7 - 0.5) + vec3(1e-4, 2e-4, 3e-4));
        const float angle = p.motion.z * age * (0.4 + 1.2 * h8) * (h1 > 0.5 ? 1.0 : -1.0);
        const mat3 R = rotation(axis, angle);
        const vec3 pivot = vec3(middle, -thick * 0.5);
        P = pivot + R * (P - pivot) + disp;
        N = R * nrm;
    }

    // Câmera em z = D olhando para a camada (z = 0), centrada nela.
    const float camera = max(p.view.z, 1.0);
    const vec2 center = p.src.xy + p.src.zw * 0.5;
    const float zc = min(P.z, 0.8 * camera);
    const float scale = camera / (camera - zc);
    const vec2 screen = center + (P.xy - center) * scale;
    const vec2 ndc = (screen - p.dst.xy) / max(p.dst.zw, vec2(1e-4)) * 2.0 - 1.0;
    // Z reverso (perto = maior), sempre dentro de (0, 1). w = 1/escala: o uv
    // da peça girada interpola com perspectiva.
    const float depth = clamp(0.5 + zc / (4.0 * camera), 0.001, 0.999);
    const float w = 1.0 / scale;
    gl_Position = vec4(ndc * w, depth * w, w);

    // Luz de cima/esquerda/frente. Parada, a frente sai com 1 (a camada intacta).
    const vec3 L = normalize(vec3(0.35, -0.45, 1.0));
    const float diffuse = abs(dot(normalize(N), L));
    float shade = mix(1.0, clamp(0.3 + 0.7 * diffuse / L.z, 0.0, 1.25), p.view.w);
    if (face == 1) shade *= mix(1.0, 0.7, p.view.w);
    if (face == 2) shade *= mix(1.0, 0.8, p.view.w);
    const float alpha = 1.0 - p.misc.x * smoothstep(0.55, 1.0, age);
    v_uv = (at - p.src.xy) / max(p.src.zw, vec2(1e-4));
    v_info = vec2(shade, alpha);
}

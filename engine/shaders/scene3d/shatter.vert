#version 450
// =============================================================================
//  Aurea / shaders / scene3d / shatter.vert
//
//  3D Shatter num plano da cena 3D: as peças com espessura são GEOMETRIA do
//  grupo — a câmera da cena, a profundidade dos modelos e das outras camadas,
//  as luzes e a gravidade do mundo. A geração das peças é a mesma do desenho
//  2D (effects/shatter3d.vert): quadrados, triângulos ou cacos de vidro, 36
//  vértices por peça (frente, verso e uma lateral por aresta). Peça parada é
//  só a frente, no lugar exato do plano (mesmo quadro que a camada sem o
//  efeito).
//
//  Espaço da camada: px, z PARA LONGE da câmera (o do plano e da extrusão).
//  As peças são geradas com z para a câmera (como no 2D) e viradas na saída.
// =============================================================================
#include "../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;
    vec4 uvRect;
    vec4 params;   // x = opacidade; y = 0 opacos com depth, 1 transparência
} pc;

#define PLANE_MAX_LIGHTS 8
// Espelho de scene3d::PlaneLightBlock + shatter3d::Uniforms (SceneRenderer.cpp).
layout(set = 0, binding = AUREA_PARAMS, std140) uniform ShatterBlock {
    mat4 worldFromLayer;
    vec4 cameraPos;
    vec4 ambient;
    vec4 lightCount;
    vec4 lightPos[PLANE_MAX_LIGHTS];
    vec4 lightColor[PLANE_MAX_LIGHTS];
    vec4 lightSpot[PLANE_MAX_LIGHTS];
    vec4 lightSpot2[PLANE_MAX_LIGHTS];
    vec4 src;      // região da camada (px)
    vec4 dst;      // (só no 2D)
    vec4 grid;     // colunas, linhas, forma (0 quadrado, 1 triângulo, 2 vidro), semente
    vec4 motion;   // explosão (0..1), força (px), giro (rad), gravidade (px, mundo)
    vec4 shape;    // espessura (px), para a câmera (−1..1), aleatoriedade, onda
    vec4 view;     // centro da força (px) xy, —, luz nas faces (0..1)
    vec4 misc;     // esmaecer, largura e altura da célula (px), maior distância ao centro
} u;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec3 v_world;
layout(location = 2) out vec3 v_normal;
layout(location = 3) flat out vec2 v_info;   // opacidade (esmaecer), face (0 frente, 1 verso, 2 lateral)
layout(location = 4) out vec3 v_layerNormal; // a normal no espaço original da camada (luz nas faces)

float hash1(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return float(x & 0xFFFFFFu) / 16777216.0;
}

uint cell_hash(ivec2 c, uint salt) {
    return (uint(c.x) * 73856093u) ^ (uint(c.y) * 19349663u) ^ (uint(u.grid.w) * 83492791u + salt);
}

vec2 grid_point(ivec2 g) {
    vec2 at = u.src.xy + vec2(g) * u.misc.yz;
    const int cols = max(int(u.grid.x + 0.5), 1);
    const int rows = max(int(u.grid.y + 0.5), 1);
    if (g.x >= cols) at.x = u.src.x + u.src.z;
    if (g.y >= rows) at.y = u.src.y + u.src.w;
    if (u.grid.z > 1.5 && g.x > 0 && g.y > 0 && g.x < cols && g.y < rows) {
        const vec2 j = vec2(hash1(cell_hash(g, 11u)), hash1(cell_hash(g, 29u))) - 0.5;
        at += j * u.misc.yz * 0.7;
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
    v_world = vec3(0.0);
    v_normal = vec3(0.0, 0.0, -1.0);
    v_layerNormal = vec3(0.0, 0.0, 1.0);
    v_info = vec2(0.0);
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   // vértice descartado: fora do recorte

    const uint vid = uint(gl_VertexIndex);
    const uint piece = vid / 36u;
    const uint k = vid % 36u;
    const int cols = max(int(u.grid.x + 0.5), 1);
    const int rows = max(int(u.grid.y + 0.5), 1);
    const bool triangles = u.grid.z > 0.5;
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

    const uint id = cell_hash(cell, 3u) * 2u + (piece & 1u);
    const float h0 = hash1(id * 747796405u + 1u);
    const vec2 toPiece = middle - u.view.xy;
    const float dist = length(toPiece);
    const float near = clamp(dist / max(u.misc.w, 1e-3), 0.0, 1.0);
    const float release = u.shape.w * mix(near, h0, u.shape.z * 0.6) * 0.6;
    const float age = clamp((u.motion.x - release) / max(1.0 - release, 1e-3), 0.0, 1.0);
    const float thick = max(u.shape.x, 0.0);
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
    float fall = 0.0;
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
        const float jitter = (h2 - 0.5) * u.shape.z * 1.2;
        const float cj = cos(jitter), sj = sin(jitter);
        dir2 = vec2(dir2.x * cj - dir2.y * sj, dir2.x * sj + dir2.y * cj);
        const vec3 dir = normalize(vec3(dir2, u.shape.y * 1.5 + (h3 - 0.5) * u.shape.z));
        const float strength = u.motion.y * (0.6 + 0.8 * h4) * (1.25 - 0.5 * near);
        const vec3 axis = normalize(vec3(h5 - 0.5, h6 - 0.5, h7 - 0.5) + vec3(1e-4, 2e-4, 3e-4));
        const float angle = u.motion.z * age * (0.4 + 1.2 * h8) * (h1 > 0.5 ? 1.0 : -1.0);
        const mat3 R = rotation(axis, angle);
        const vec3 pivot = vec3(middle, -thick * 0.5);
        P = pivot + R * (P - pivot) + dir * strength * age;
        N = R * nrm;
        fall = u.motion.w * age * age;
    }

    // Para o espaço do plano (z para longe) e para o mundo. A gravidade é do
    // MUNDO (+Y = para baixo): numa camada girada as peças caem para o chão
    // da cena, não para a "base" da camada.
    vec3 local = vec3(P.xy, -P.z);
    vec3 localN = vec3(N.xy, -N.z);
    const mat3 linear = mat3(u.worldFromLayer);
    const float det = determinant(linear);
    const bool invertible = abs(det) > 1e-12;
    const mat3 layerFromWorld = invertible ? inverse(linear) : mat3(1.0);
    if (fall != 0.0 && invertible) local += layerFromWorld * vec3(0.0, fall, 0.0);

    gl_Position = pc.clipFromLayer * vec4(local, 1.0);
    v_world = (u.worldFromLayer * vec4(local, 1.0)).xyz;
    const vec3 worldN = invertible ? transpose(layerFromWorld) * localN : localN;
    v_normal = dot(worldN, worldN) > 1e-20 ? normalize(worldN) : vec3(0.0, 0.0, -1.0);
    v_layerNormal = N;
    v_uv = (at - u.src.xy) / max(u.src.zw, vec2(1e-4));
    v_info = vec2(1.0 - u.misc.x * smoothstep(0.55, 1.0, age), float(face));
}

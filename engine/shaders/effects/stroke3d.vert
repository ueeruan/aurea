#version 450
// =============================================================================
//  Aurea / shaders / effects / stroke3d.vert
//
//  3D Stroke: um traço ao longo de um caminho (as máscaras da camada ou uma
//  forma pronta) girado em 3D, visto em perspectiva, com início/fim animáveis,
//  afinamento nas pontas, degradê de cor e repetições em profundidade. Um
//  quadrado de tela por trecho do caminho (6 vértices, sem vertex buffer); o
//  fragmento arredonda o trecho numa cápsula, então as junções saem lisas.
//  Implementação própria; a referência de comportamento é o "3D Stroke" dos
//  editores de movimento.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // o caminho (x, y, comprimento acumulado, continua)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 dst;      // região da saída (px)
    vec4 path;     // nº de pontos, comprimento total, fechado (0/1), px por texel
    vec4 window;   // início (0..1), fim (0..1), deslocamento (0..1), espessura (px)
    vec4 taper;    // afinar no início (0..1), afinar no fim (0..1), opacidade, -
    vec4 rot;      // rotação X, Y, Z (rad), distância da câmera (px)
    vec4 center;   // centro (px) xy, nº de cópias, esmaecer cópias (0..1)
    vec4 copies;   // distância Z entre cópias (px), giro por cópia (rad), escala por cópia, -
    vec4 color0;   // cor do início (linear, pré-multiplicada)
    vec4 color1;   // cor do fim
    vec4 compose;  // (composição)
} p;

layout(location = 0) out vec2 v_pos;                 // px da camada
layout(location = 1) flat out vec4 v_seg;            // a, b (px)
layout(location = 2) flat out vec4 v_radius;         // raio em a, raio em b, opacidade, margem
layout(location = 3) flat out vec4 v_colorA;
layout(location = 4) flat out vec4 v_colorB;

mat3 rot_x(float a) { const float c = cos(a), s = sin(a); return mat3(1, 0, 0, 0, c, s, 0, -s, c); }
mat3 rot_y(float a) { const float c = cos(a), s = sin(a); return mat3(c, 0, -s, 0, 1, 0, s, 0, c); }
mat3 rot_z(float a) { const float c = cos(a), s = sin(a); return mat3(c, s, 0, -s, c, 0, 0, 0, 1); }

// Ponto do caminho (relativo ao centro) → tela (px), com a escala da
// perspectiva e a profundidade.
vec3 project(vec2 q, mat3 R, float scale, float zOffset, out float depthZ) {
    vec3 P = R * vec3(q * scale, 0.0);
    P.z += zOffset;
    const float camera = max(p.rot.w, 1.0);
    const float zc = min(P.z, 0.8 * camera);
    const float k = camera / (camera - zc);
    depthZ = zc;
    return vec3(p.center.xy + P.xy * k, k);
}

float width_at(float t, float w0, float w1) {
    const float span = max(w1 - w0, 1e-4);
    const float u = clamp((t - w0) / span, 0.0, 1.0);
    float f = 1.0;
    if (p.taper.x > 1e-4) f *= clamp(u / p.taper.x, 0.0, 1.0);
    if (p.taper.y > 1e-4) f *= clamp((1.0 - u) / p.taper.y, 0.0, 1.0);
    return f;
}

void main() {
    v_pos = vec2(0.0);
    v_seg = vec4(0.0);
    v_radius = vec4(0.0);
    v_colorA = vec4(0.0);
    v_colorB = vec4(0.0);
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);

    const int count = int(p.path.x + 0.5);
    if (count < 2) return;
    const uint vid = uint(gl_VertexIndex);
    const uint perCopy = uint(count - 1) * 6u;
    // Do fundo para a frente: a cópia mais longe desenha primeiro.
    const uint copies = uint(max(p.center.z, 1.0) + 0.5);
    const uint copy = (copies - 1u) - min(vid / perCopy, copies - 1u);
    const int seg = int((vid % perCopy) / 6u);
    const uint corner = vid % 6u;

    const vec4 a = texelFetch(u_tex1, ivec2(seg, 0), 0);
    const vec4 b = texelFetch(u_tex1, ivec2(seg + 1, 0), 0);
    if (b.w < 0.5) return;   // começo de outro caminho: não liga com o anterior

    // Janela visível (comprimento): início/fim + deslocamento; no caminho
    // fechado ela dá a volta (escolhe a cópia da janela que mais cobre o trecho).
    const float total = max(p.path.y, 1e-4);
    float w0 = (p.window.x + p.window.z) * total;
    float w1 = (p.window.y + p.window.z) * total;
    if (p.path.z > 0.5) {
        const float base0 = w0 - floor(w0 / total) * total;
        const float base1 = base0 + (w1 - w0);
        float best = -1e30;
        for (int i = -1; i <= 1; ++i) {
            const float o = float(i) * total;
            const float cover = min(b.z, base1 + o) - max(a.z, base0 + o);
            if (cover > best) { best = cover; w0 = base0 + o; w1 = base1 + o; }
        }
    }
    const float t0 = max(a.z, w0), t1 = min(b.z, w1);
    if (t1 <= t0) return;
    const float len = max(b.z - a.z, 1e-6);
    const vec2 qa = mix(a.xy, b.xy, (t0 - a.z) / len);
    const vec2 qb = mix(a.xy, b.xy, (t1 - a.z) / len);

    // A cópia `copy`: mais funda, girada e escalada.
    const float ci = float(copy);
    const mat3 R = rot_x(p.rot.x) * rot_y(p.rot.y) * rot_z(p.rot.z + ci * p.copies.y);
    const float scale = pow(max(p.copies.z, 1e-3), ci);
    float za, zb;
    const vec3 sa = project(qa, R, scale, -ci * p.copies.x, za);
    const vec3 sb = project(qb, R, scale, -ci * p.copies.x, zb);
    const float halfWidth = 0.5 * p.window.w;
    const float ra = halfWidth * sa.z * width_at(t0, w0, w1);
    const float rb = halfWidth * sb.z * width_at(t1, w0, w1);
    if (max(ra, rb) <= 0.0) return;

    // Quadrado em volta do trecho, com um texel de folga (antialiasing).
    const float margin = max(p.path.w, 1e-3);
    const float rr = max(ra, rb) + margin;
    vec2 dir = sb.xy - sa.xy;
    const float dl = length(dir);
    dir = dl > 1e-5 ? dir / dl : vec2(1.0, 0.0);
    const vec2 nrm = vec2(-dir.y, dir.x);
    const vec2 ends[2] = vec2[2](sa.xy - dir * rr, sb.xy + dir * rr);
    const float sides[2] = float[2](-1.0, 1.0);
    const uint endIndex[6] = uint[6](0u, 1u, 1u, 0u, 1u, 0u);
    const uint sideIndex[6] = uint[6](0u, 0u, 1u, 0u, 1u, 1u);
    const vec2 at = ends[endIndex[corner]] + nrm * (sides[sideIndex[corner]] * rr);

    const vec2 ndc = (at - p.dst.xy) / max(p.dst.zw, vec2(1e-4)) * 2.0 - 1.0;
    const float camera = max(p.rot.w, 1.0);
    // Z reverso (perto = maior): o trecho mais perto cobre o mais longe.
    const float depth = clamp(0.5 + 0.5 * (za + zb) / (4.0 * camera), 0.001, 0.999);
    gl_Position = vec4(ndc, depth, 1.0);

    const float total_copies = float(copies);
    const float fade = total_copies > 1.0 ? 1.0 - p.center.w * ci / (total_copies - 1.0) : 1.0;
    const float ua = clamp((t0 - w0) / max(w1 - w0, 1e-4), 0.0, 1.0);
    const float ub = clamp((t1 - w0) / max(w1 - w0, 1e-4), 0.0, 1.0);
    v_pos = at;
    v_seg = vec4(sa.xy, sb.xy);
    v_radius = vec4(ra, rb, p.taper.z * fade, margin);
    v_colorA = mix(p.color0, p.color1, ua);
    v_colorB = mix(p.color0, p.color1, ub);
}

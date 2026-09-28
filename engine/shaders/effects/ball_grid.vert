#version 450
// =============================================================================
//  Aurea / shaders / effects / ball_grid.vert
//
//  Bolas: uma bola por célula da grade, gerada aqui (sem vertex buffer; 6
//  vértices por bola, um quadrado que o fragmento arredonda). A cor é a da
//  camada no centro da célula; célula transparente não desenha.
//
//  Posição da bola (px da camada, relativa ao centro, z para a câmera):
//    + dispersão      deslocamento aleatório FIXO por bola (hash do índice)
//    + instabilidade  vaivém por bola: direção e fase sorteadas, seno do
//                     "estado" (360° fecha o ciclo)
//    × rotação        no(s) eixo(s) escolhido(s), pelo ângulo de rotação mais
//                     a torção × o fator da propriedade (posição, distância,
//                     canal de cor...) daquela bola
//  e a perspectiva de uma câmera a `F` px à frente da camada.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 grid;     // colunas, linhas, espaçamento (px), raio (px)
    vec4 motion;   // dispersão (px), rotação (rad), torção (rad), propriedade da torção
    vec4 region;   // região da imagem (px da camada)
    vec4 extra;    // eixo, estado (rad), instabilidade (px), distância focal (px)
    vec4 center;   // centro (px), tamanho natural da camada
    vec4 texel;    // texels por px (x, y), px por texel
} p;

layout(location = 0) out vec2 v_local;          // |v_local| = 1 na borda da bola
layout(location = 1) flat out vec4 v_color;     // cor (sem pré-multiplicar) e alfa
layout(location = 2) flat out float v_radius;   // raio na tela, em texels

float hash1(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return float(x & 0xFFFFFFu) / 16777216.0;
}

mat3 rot_x(float a) { const float c = cos(a), s = sin(a); return mat3(1, 0, 0, 0, c, s, 0, -s, c); }
mat3 rot_y(float a) { const float c = cos(a), s = sin(a); return mat3(c, 0, -s, 0, 1, 0, s, 0, c); }
mat3 rot_z(float a) { const float c = cos(a), s = sin(a); return mat3(c, s, 0, -s, c, 0, 0, 0, 1); }

void main() {
    const vec2 corners[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    const uint vid = uint(gl_VertexIndex);
    const uint ball = vid / 6u;
    const vec2 q = corners[vid % 6u];
    const uint cols = max(uint(p.grid.x + 0.5), 1u);
    const vec2 cell = vec2(float(ball % cols), float(ball / cols));
    const vec2 cellPx = p.region.xy + (cell + 0.5) * p.grid.z;
    const vec4 c = textureLod(u_tex0, (cellPx - p.region.xy) / max(p.region.zw, vec2(1e-4)), 0.0);
    v_local = q;
    v_radius = 0.0;
    v_color = vec4(0.0);
    if (c.a < 0.004 || p.grid.w <= 0.0) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   // fora do recorte: nenhum fragmento
        return;
    }
    const vec3 rgb = c.rgb / c.a;

    vec3 pos = vec3(cellPx - p.center.xy, 0.0);
    const uint id = ball * 7u + 3u;
    const vec3 rnd = vec3(hash1(id * 747796405u + 1u), hash1(id * 2891336453u + 2u), hash1(id * 1597334677u + 3u));
    pos += (rnd * 2.0 - 1.0) * p.motion.x;
    const vec3 wob = vec3(hash1(id * 3266489917u + 4u), hash1(id * 668265263u + 5u), hash1(id * 374761393u + 6u)) * 2.0 - 1.0;
    const float phase = hash1(id * 2246822519u + 7u) * 6.28318530718;
    pos += normalize(wob + vec3(1e-4)) * p.extra.z * sin(p.extra.y + phase);

    // Fator da torção desta bola.
    const vec2 rel = cellPx - p.center.xy;
    const vec2 halfSize = max(p.center.zw * 0.5, vec2(1.0));
    const int prop = int(p.motion.w + 0.5);
    float f;
    if (prop == 0) f = rel.x / (2.0 * halfSize.x);
    else if (prop == 1) f = rel.y / (2.0 * halfSize.y);
    else if (prop == 2) f = abs(rel.x) / halfSize.x;
    else if (prop == 3) f = abs(rel.y) / halfSize.y;
    else if (prop == 4) f = length(rel) / length(halfSize);
    else if (prop == 5) f = length(cellPx) / max(length(p.center.zw), 1.0);
    else if (prop == 6) f = rgb.r;
    else if (prop == 7) f = rgb.g;
    else if (prop == 8) f = rgb.b;
    else if (prop == 9) f = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    else f = c.a;
    const float ang = p.motion.y + p.motion.z * f;

    const int axis = int(p.extra.x + 0.5);
    mat3 R;
    if (axis == 0) R = rot_x(ang);
    else if (axis == 1) R = rot_y(ang);
    else if (axis == 2) R = rot_z(ang);
    else if (axis == 3) R = rot_y(ang) * rot_x(ang);
    else if (axis == 4) R = rot_z(ang) * rot_x(ang);
    else if (axis == 5) R = rot_z(ang) * rot_y(ang);
    else R = rot_z(ang) * rot_y(ang) * rot_x(ang);
    pos = R * pos;

    // Perspectiva: câmera em z = F olhando para a camada (z = 0).
    const float F = max(p.extra.w, 1.0);
    const float k = F / max(F - pos.z, 0.05 * F);
    const vec2 screen = p.center.xy + pos.xy * k;
    const float rad = p.grid.w * k;
    const float margin = max(p.texel.z, 1e-3);   // um texel, em px
    const vec2 at = screen + q * (rad + margin);
    const vec2 ndc = (at - p.region.xy) / max(p.region.zw, vec2(1e-4)) * 2.0 - 1.0;
    // Z reverso (perto = maior), sempre dentro de (0, 1).
    const float z = clamp(0.5 + pos.z / (4.0 * F), 0.001, 0.999);
    gl_Position = vec4(ndc, z, 1.0);
    v_local = q * (rad + margin) / max(rad, 1e-4);
    v_color = vec4(rgb, c.a);
    v_radius = rad / margin;
}

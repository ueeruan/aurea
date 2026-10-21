#version 450
// =============================================================================
//  Aurea / shaders / effects / constellation.vert
//
//  Constelação (3D): pontos ligados por linhas quando estão perto, com faces
//  (triângulos) entre trios ligados. Tudo é PLANEJADO na CPU
//  (ConstellationEffect.cpp: geometria, ruído, campo, vizinhos, câmera) e
//  chega aqui numa textura de DADO RGBA32F, lida por texelFetch — sem vertex
//  buffer. O ponto já vem projetado em px da camada, com o fator de
//  perspectiva, a névoa e a cor.
//
//  Layout da textura (um índice linear, `counts.w` texels por linha):
//    ponto i:      [2i]       = (x, y, perspectiva, alfa da névoa)
//                  [2i + 1]   = (r, g, b lineares, 1 = desenhar o sprite)
//    linha l:      [2P + l]   = (i, j, alfa pela distância, -)
//    triângulo t:  [2P+L+t]   = (i, j, k, alfa pelo maior lado)
//
//  Vértices pelo gl_VertexIndex, na ordem de desenho:
//    [0, 3T)          triângulos, por baixo;
//    [3T, 3T + 6L)    linhas: um quadrado na tela, da espessura pedida;
//    depois, 6P       pontos: um quadrado que o fragmento arredonda.
//  Sem vetor local (os cantos saem de aritmética): o compilador da Adreno
//  tropeça em arrays constantes no vértice.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_data;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;     // região de saída (px da camada)
    vec4 counts;     // x = pontos, y = linhas, z = triângulos, w = texels por linha do dado
    vec4 texel;      // x = texels por px da camada, y = teto do diâmetro do ponto (px), zw = -
    vec4 line;       // x = espessura (px), y = com perspectiva (0/1), z = opacidade, w = cor dos pontos (0/1)
    vec4 lineColor;  // cor própria das linhas (linear)
    vec4 point;      // x = diâmetro (px), y = com perspectiva (0/1), z = opacidade, w = suavidade 0..1
    vec4 tri;        // x = opacidade, y = cor dos pontos (0/1), zw = -
    vec4 triColor;   // cor própria dos triângulos (linear)
} p;

layout(location = 0) out vec4 v_color;       // linear, alfa reto
layout(location = 1) out vec2 v_coord;       // ponto: texels desde o centro; linha: (meia largura, através)
layout(location = 2) flat out vec4 v_shape;  // x = tipo (0 face, 1 linha, 2 ponto), y = raio (texels), z = suavidade

vec4 fetch(int index) {
    const int w = max(int(p.counts.w + 0.5), 1);
    return texelFetch(u_data, ivec2(index % w, index / w), 0);
}

vec4 to_clip(vec2 layerPx) {
    return vec4((layerPx - p.region.xy) / max(p.region.zw, vec2(1e-4)) * 2.0 - 1.0, 0.5, 1.0);
}

void cull() {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   // fora do recorte: nenhum fragmento
    v_color = vec4(0.0);
    v_coord = vec2(0.0);
    v_shape = vec4(0.0);
}

// Um canto de triângulo: a posição e a névoa do ponto; a cor é a do ponto
// ou a própria das faces.
void face_vertex(int vid, int points, int lines) {
    const int t = vid / 3;
    const int corner = vid - t * 3;
    const vec4 f = fetch(2 * points + lines + t);
    const float pick = corner == 0 ? f.x : (corner == 1 ? f.y : f.z);
    const int i = int(pick + 0.5);
    const vec4 a = fetch(2 * i);
    const vec3 col = p.tri.y > 0.5 ? fetch(2 * i + 1).rgb : p.triColor.rgb;
    const float alpha = f.w * p.tri.x * a.w;
    if (!(alpha > 0.0)) { cull(); return; }
    v_color = vec4(col, alpha);
    v_coord = vec2(0.0);
    v_shape = vec4(0.0);
    gl_Position = to_clip(a.xy);
}

// Uma linha por 6 vértices: o quadrado entre os dois centros, com a meia
// largura (texels) e 1 texel de borda suave. Mais fina que 1 texel, desenha
// com 1 e tira a diferença do alfa (a mesma luz no preview em 1/4).
void line_vertex(int vid, int points) {
    const int l = vid / 6;
    const int c = vid - l * 6;
    const vec4 d = fetch(2 * points + l);
    const int i = int(d.x + 0.5);
    const int j = int(d.y + 0.5);
    const vec4 a = fetch(2 * i);
    const vec4 b = fetch(2 * j);
    const float t = (c == 1 || c == 4 || c == 5) ? 1.0 : 0.0;
    const float side = (c == 2 || c == 3 || c == 5) ? 1.0 : -1.0;
    const vec2 ab = b.xy - a.xy;
    const float len = length(ab);
    const vec2 along = len > 1e-4 ? ab / len : vec2(1.0, 0.0);
    const vec2 normal = vec2(-along.y, along.x);
    const float scale = max(p.texel.x, 1e-6);
    const float persp = mix(1.0, mix(a.z, b.z, t), clamp(p.line.y, 0.0, 1.0));
    float widthTex = max(p.line.x * persp, 0.0) * scale;
    float alpha = d.z * p.line.z * mix(a.w, b.w, t);
    if (widthTex < 1.0) { alpha *= widthTex; widthTex = 1.0; }
    if (!(alpha > 0.0)) { cull(); return; }
    const float hw = widthTex * 0.5;
    const float extent = hw + 1.0;
    const vec3 col = p.line.w > 0.5 ? mix(fetch(2 * i + 1).rgb, fetch(2 * j + 1).rgb, t) : p.lineColor.rgb;
    v_color = vec4(col, alpha);
    v_coord = vec2(hw, side * extent);
    v_shape = vec4(1.0, 0.0, 0.0, 0.0);
    gl_Position = to_clip(mix(a.xy, b.xy, t) + normal * (side * extent / scale));
}

// Um ponto por 6 vértices: quadrado de frente para a tela; o fragmento faz
// o disco (com a suavidade pedida). Menor que 1,5 texel, desenha com 1,5 e
// tira a área do alfa.
void point_vertex(int vid) {
    const int i = vid / 6;
    const int c = vid - i * 6;
    const vec4 a = fetch(2 * i);
    const vec4 col = fetch(2 * i + 1);
    const vec2 corner = vec2((c == 1 || c == 4 || c == 5) ? 1.0 : -1.0, (c == 2 || c == 3 || c == 5) ? 1.0 : -1.0);
    const float scale = max(p.texel.x, 1e-6);
    const float persp = mix(1.0, a.z, clamp(p.point.y, 0.0, 1.0));
    float diameter = min(max(p.point.x * persp, 0.0), max(p.texel.y, 0.0)) * scale;
    float alpha = p.point.z * a.w * col.w;
    if (diameter < 1.5) { alpha *= diameter * diameter / 2.25; diameter = 1.5; }
    if (!(alpha > 0.0)) { cull(); return; }
    const float radius = diameter * 0.5;
    const float extent = radius + 1.0;
    v_color = vec4(col.rgb, alpha);
    v_coord = corner * extent;
    v_shape = vec4(2.0, radius, clamp(p.point.w, 0.0, 1.0), 0.0);
    gl_Position = to_clip(a.xy + corner * (extent / scale));
}

void main() {
    const int points = int(p.counts.x + 0.5);
    const int lines = int(p.counts.y + 0.5);
    const int tris = int(p.counts.z + 0.5);
    int vid = gl_VertexIndex;
    if (vid < 3 * tris) { face_vertex(vid, points, lines); return; }
    vid -= 3 * tris;
    if (vid < 6 * lines) { line_vertex(vid, points); return; }
    vid -= 6 * lines;
    if (vid < 6 * points) { point_vertex(vid); return; }
    cull();
}

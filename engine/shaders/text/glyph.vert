#version 450
// =============================================================================
//  Aurea / shaders / text / glyph.vert
//
//  Um quad por glifo (instância): retângulo em px da layer, UV no atlas SDF e
//  o transform da própria letra (2×2 em volta do pivô + deslocamento) — é por
//  aqui que o Text Animator mexe em cada caractere sem rasterizar nada.
// =============================================================================
#include "../common/bindings.glsl"

struct Glyph {
    vec4 rect;     // x0 y0 x1 y1 (px da layer)
    vec4 uv;       // u0 v0 u1 v1
    vec4 fill;     // cor linear, alfa
    vec4 stroke;   // cor linear, alfa
    vec4 xf;       // matriz 2×2 (a b c d): x' = a·x + c·y, y' = b·x + d·y
    vec4 misc;     // tx, ty, k (px da layer por px da base), largura do contorno (px da layer)
    vec4 pivot;    // px, py, desfoque (px), _
};

layout(set = 0, binding = AUREA_DATA, std430) readonly buffer Glyphs { Glyph g[]; } glyphs;

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 params;   // x = primeiro glifo desta camada no buffer
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out int v_index;

const vec2 kCorners[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

void main() {
    const int idx = int(pc.params.x) + gl_InstanceIndex;
    const Glyph gl = glyphs.g[idx];
    const vec2 c = kCorners[gl_VertexIndex];
    const vec2 p = mix(gl.rect.xy, gl.rect.zw, c);
    const vec2 d = p - gl.pivot.xy;
    const vec2 q = gl.pivot.xy + vec2(gl.xf.x * d.x + gl.xf.z * d.y, gl.xf.y * d.x + gl.xf.w * d.y) + gl.misc.xy;
    // Sólido (fundo): uv = posição local em px, para o retângulo arredondado.
    v_uv = gl.uv.x < 0.0 ? c * (gl.rect.zw - gl.rect.xy) : mix(gl.uv.xy, gl.uv.zw, c);
    v_index = idx;
    gl_Position = pc.clipFromLayer * vec4(q, 0.0, 1.0);
}

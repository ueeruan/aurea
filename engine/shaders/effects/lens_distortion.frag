#version 450
#include "../common/bindings.glsl"
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap; vec4 texel; vec4 p0; vec4 p1; vec4 p2; vec4 p3; vec4 color;
} p;
// p0: k1, k2, direção (0 remover distorção, 1 aplicar), escala
// p1: centro (px da camada), meia altura da camada (px)
// p2: região da entrada (x, y, largura, altura em px da camada)
// O modelo do Rastreio de câmera 3D (tracking::lens_distort): coordenadas
// normalizadas pela meia altura, p_d = p_u·(1 + k1·r² + k2·r⁴).
vec4 atPoint(vec2 point) {
    vec2 uv = (point - p.p2.xy) / p.p2.zw;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec4(0.0);
    return texture(u_tex0, uv);
}
vec2 distortP(vec2 q) {
    float r2 = dot(q, q);
    return q * (1.0 + p.p0.x * r2 + p.p0.y * r2 * r2);
}
// Inverso por ponto fixo (o mesmo de tracking::lens_undistort).
vec2 undistortP(vec2 d, out bool ok) {
    vec2 q = d;
    ok = true;
    for (int i = 0; i < 16; ++i) {
        float r2 = dot(q, q);
        float f = 1.0 + p.p0.x * r2 + p.p0.y * r2 * r2;
        if (f < 0.1) { ok = false; return q; }
        q = d / f;
    }
    return q;
}
void main() {
    vec2 point = p.p2.xy + v_uv * p.p2.zw;
    float half_h = max(p.p1.z, 1.0);
    vec2 q = (point - p.p1.xy) / half_h / max(p.p0.w, 0.01);
    vec2 s;
    if (p.p0.z < 0.5) {
        // Remover: o pixel de saída é o ponto sem distorção; a fonte filmada
        // tem ele no ponto distorcido.
        s = distortP(q);
    } else {
        // Aplicar: o pixel de saída é o ponto distorcido; a CG (sem
        // distorção) tem ele no inverso.
        bool ok;
        s = undistortP(q, ok);
        if (!ok) { o_color = vec4(0.0); return; }
    }
    o_color = atPoint(p.p1.xy + s * half_h);
}

#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_cylinder.frag
//
//  Cilindro (o comportamento do CC Cylinder): a largura da camada dá uma volta
//  inteira num tubo de eixo vertical, sem tampas, visto de frente sem
//  perspectiva, girado em X/Y/Z e iluminado.
//
//  Um raio por pixel (direção −z) é levado ao espaço do objeto e cortado com
//  x² + z² = R²: a raiz mais perto é a parede de FORA virada para quem vê; a
//  mais longe, a parede de DENTRO do outro lado (vista pela boca do tubo ou
//  através da frente transparente).
//
//  p0..p2.xyz = linhas da rotação inversa; p0.w = raio, p1.w = modo
//  (0 completo, 1 por fora, 2 por dentro), p2.w = meia altura; p3.xy = centro.
// =============================================================================
#include "cc_common.glsl"
#include "cc_light.glsl"

vec3 to_object(vec3 v) { return vec3(dot(p.p0.xyz, v), dot(p.p1.xyz, v), dot(p.p2.xyz, v)); }
vec3 to_view(vec3 n) { return n.x * p.p0.xyz + n.y * p.p1.xyz + n.z * p.p2.xyz; }

/// A parede no ponto `o` (objeto): cor iluminada e cobertura da borda de cima/baixo.
vec4 wall(vec3 o, float R, float H, bool inside) {
    const float u = 0.5 + atan(o.x, o.z) / (2.0 * CC_PI);
    const float v = 0.5 + o.y / (2.0 * H);
    const vec4 c = cc_at(p.box.xy + vec2(u, v) * p.box.zw);
    vec3 n = to_view(vec3(o.x, 0.0, o.z) / R);
    if (inside) n = -n;
    const float rim = clamp((H - abs(o.y)) / cc_texel() + 0.5, 0.0, 1.0);
    return vec4(cc_shade(c.rgb, c.a, n), c.a) * rim;
}

void main() {
    const float R = max(p.p0.w, 1e-3);
    const float H = max(p.p2.w, 1e-3);
    const int mode = int(p.p1.w + 0.5);
    const vec3 O = to_object(vec3(cc_px() - p.p3.xy, 0.0));
    const vec3 D = to_object(vec3(0.0, 0.0, -1.0));
    const float a = D.x * D.x + D.z * D.z;
    if (a < 1e-6) { o_color = vec4(0.0); return; }   // olhando pelo eixo: só a borda
    const float b = O.x * D.x + O.z * D.z;            // meio b do polinômio
    const float c = O.x * O.x + O.z * O.z;
    // Distância entre o raio e o eixo = distância (na tela) à silhueta lateral.
    const float axis = sqrt(max(c - b * b / a, 0.0));
    const float side = clamp((R - axis) / cc_texel() + 0.5, 0.0, 1.0);
    if (side <= 0.0) { o_color = vec4(0.0); return; }
    const float root = sqrt(max(b * b - a * (c - R * R), 0.0));
    const float t1 = (-b - root) / a, t2 = (-b + root) / a;
    vec4 front = vec4(0.0);
    if (mode != 2) front = wall(O + t1 * D, R, H, false);
    vec4 result = front;
    if (mode != 1 && front.a < 0.999) result = front + wall(O + t2 * D, R, H, true) * (1.0 - front.a);
    o_color = result * side;
}

#version 450
// =============================================================================
//  Aurea / shaders / effects / cc_sphere.frag
//
//  Esfera (o comportamento do CC Sphere): a camada embrulha uma esfera como
//  um mapa-múndi (equirretangular), vista de frente sem perspectiva, girada em
//  X/Y/Z e iluminada. Sem rotação, o centro da camada olha para quem vê.
//
//  Cada pixel do disco é um ponto da esfera unitária: a frente (z > 0) e, por
//  trás dela, a parede de DENTRO do fundo (z < 0), vista quando a frente é
//  transparente ou no modo "Por dentro".
//
//  p0..p2.xyz = linhas da rotação inversa (vista → objeto); p0.w = raio (px),
//  p1.w = modo (0 completo, 1 por fora, 2 por dentro); p3.xy = centro (px).
// =============================================================================
#include "cc_common.glsl"
#include "cc_light.glsl"

vec4 surface(vec3 v) {
    const vec3 o = vec3(dot(p.p0.xyz, v), dot(p.p1.xyz, v), dot(p.p2.xyz, v));
    const float lon = atan(o.x, o.z);                     // 0 = de frente
    const float lat = asin(clamp(o.y, -1.0, 1.0));        // y para baixo
    const vec2 uv = vec2(0.5 + lon / (2.0 * CC_PI), 0.5 + lat / CC_PI);
    return cc_at(p.box.xy + uv * p.box.zw);
}

void main() {
    const float R = max(p.p0.w, 1e-3);
    const vec2 d = (cc_px() - p.p3.xy) / R;
    const float rr = dot(d, d);
    // Borda do disco com um texel de antisserrilhado.
    const float edge = clamp((1.0 - sqrt(rr)) * R / cc_texel() + 0.5, 0.0, 1.0);
    if (edge <= 0.0) { o_color = vec4(0.0); return; }
    const float z = sqrt(max(1.0 - rr, 0.0));
    const int mode = int(p.p1.w + 0.5);
    vec4 front = vec4(0.0);
    if (mode != 2) {
        const vec3 n = vec3(d, z);
        const vec4 c = surface(n);
        front = vec4(cc_shade(c.rgb, c.a, n), c.a);
    }
    vec4 result = front;
    if (mode != 1 && front.a < 0.999) {
        // A parede de dentro do fundo: a normal aponta para o centro (e para quem vê).
        const vec4 c = surface(vec3(d, -z));
        const vec4 back = vec4(cc_shade(c.rgb, c.a, vec3(-d, z)), c.a);
        result = front + back * (1.0 - front.a);
    }
    o_color = result * edge;
}

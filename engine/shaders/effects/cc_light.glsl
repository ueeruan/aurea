// =============================================================================
//  Aurea / shaders / effects / cc_light.glsl
//
//  A luz dos efeitos CC com superfície (Esfera, Cilindro, Bolhas, Vidro):
//  ambiente + difusa (Lambert) + especular (Blinn-Phong) com o observador em
//  +z. Espaço da tela: x para a direita, y para BAIXO, z para quem olha.
//
//  Uniforms (cc::light_uniforms):
//    p4.xyz = direção PARA a luz (normalizada), p4.w = intensidade
//    p5     = ambiente, difusa, especular, expoente do brilho
//    color  = cor da luz (linear), a = metal (0 = brilho branco, 1 = da cor)
//
//  `base` é a cor pré-multiplicada da superfície; o brilho é multiplicado pelo
//  alfa para continuar pré-multiplicado.
// =============================================================================
#ifndef AUREA_CC_LIGHT_GLSL
#define AUREA_CC_LIGHT_GLSL

vec3 cc_shade(vec3 base, float alpha, vec3 n) {
    const vec3 L = p.p4.xyz;
    const vec3 light = p.color.rgb * p.p4.w;
    const float ndl = max(dot(n, L), 0.0);
    const vec3 hv = normalize(L + vec3(0.0, 0.0, 1.0));
    const float spec = pow(max(dot(n, hv), 0.0), max(p.p5.w, 1.0));
    const vec3 straight = alpha > 1e-4 ? base / alpha : vec3(0.0);
    const vec3 tint = mix(vec3(1.0), straight, clamp(p.color.a, 0.0, 1.0));
    return base * (p.p5.x + p.p5.y * ndl * light) + light * (spec * p.p5.z * alpha) * tint;
}

#endif

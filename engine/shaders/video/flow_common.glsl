// =============================================================================
//  Aurea / shaders / video / flow_common.glsl
//
//  Cor do optical flow (1.0.3, filmagem real). A luminância sozinha não separa
//  pele de parede verde-água, camisa vermelha de cortina marrom: o contorno
//  existe só na crominância e o fluxo vazava por ele. Aqui a cor vira Y'CbCr
//  na mesma curva perceptual da pirâmide (raiz da luz linear), e a distância
//  entre duas cores pesa a crominância junto com a luminância.
// =============================================================================
#ifndef AUREA_FLOW_COMMON_GLSL
#define AUREA_FLOW_COMMON_GLSL

// R'G'B' perceptual → (Cb, Cr), BT.709. Faixa ±0,5.
vec2 flow_chroma(vec3 linearRgb) {
    const vec3 p = sqrt(max(linearRgb, vec3(0.0)));
    const float y = dot(p, vec3(0.2126, 0.7152, 0.0722));
    return vec2((p.b - y) * 0.5389, (p.r - y) * 0.6350);
}

// Distância² de cor: luminância + crominância (peso `k` na crominância).
float flow_color_dist2(float dl, vec2 dc, float k) {
    return dl * dl + k * dot(dc, dc);
}

// Y'CbCr perceptual de um pixel da imagem (a deformação compara cores assim).
vec3 flow_ycc(vec3 linearRgb) {
    const vec3 p = sqrt(max(linearRgb, vec3(0.0)));
    const float y = dot(p, vec3(0.2126, 0.7152, 0.0722));
    return vec3(y, (p.b - y) * 0.5389, (p.r - y) * 0.6350);
}

#endif

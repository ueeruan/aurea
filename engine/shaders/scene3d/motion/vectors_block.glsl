// =============================================================================
//  Aurea / shaders / scene3d / motion / vectors_block.glsl
//
//  O bloco de uniform de um desenho do passe de velocidade (espelho de
//  VelocityBlock em SceneRenderer.cpp — 144 bytes, std140).
// =============================================================================
#ifndef AUREA_MOTION_VECTORS_BLOCK_GLSL
#define AUREA_MOTION_VECTORS_BLOCK_GLSL

layout(set = 0, binding = AUREA_PARAMS, std140) uniform VelocityBlock {
    mat4 clipFromLocal;  // centro do obturador: a mesma matriz do passe PBR
    vec4 baseColor;      // a = alfa do material (recorte MASK)
    vec4 alpha;          // x = corte, y = modo (0 opaco, 1 recorte), z = início das juntas, w = conjunto de UV (0/1)
    vec4 uvXform;        // offset.xy, escala.zw da cor base
    vec4 uvRot;          // x = rotação (rad), y = tem textura, zw = metade do tamanho do alvo (px)
    vec4 limits;         // x = maior rastro (px)
} u;

#endif

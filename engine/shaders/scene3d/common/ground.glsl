// =============================================================================
//  Aurea / shaders / scene3d / common / ground.glsl
//
//  O bloco do CHÃO do grupo 3D (ground.vert/.frag) — espelho exato de
//  GroundBlock em src/scene3d/Ground.cpp. Bloco próprio (não o SceneBlock do
//  PBR): o chão não tem material glTF, e mudar o bloco dos modelos não mexe
//  aqui. Mundo = composição: px, Y PARA BAIXO ("para cima" = −Y).
// =============================================================================
#ifndef AUREA_GROUND_GLSL
#define AUREA_GROUND_GLSL

#include "../../common/bindings.glsl"

#define GROUND_MAX_LIGHTS 4

layout(set = 0, binding = AUREA_PARAMS, std140) uniform GroundBlock {
    mat4 viewProj;          // clip ← mundo
    vec4 cameraPos;         // xyz mundo; w = exposição do objeto (1)
    vec4 center;            // x, y (altura do chão), z; w = meia largura do quadrado
    vec4 color;             // rgb linear; w = modo (1 visível, 2 só sombra/reflexo)
    vec4 params;            // x = rugosidade, y = refletividade, z = força da sombra de contato, w = raio do fim do desbotamento
    vec4 envParams;         // x = intensidade, y = tem IBL, z = mips do especular, w = giro (rad)
    vec4 viewport;          // xy = 1/tamanho do alvo, z = tem reflexo planar, w = raio do início do desbotamento
    vec4 skyColor;          // ambiente analítico (sem IBL)
    vec4 groundColor;
    vec4 lightCount;        // x = luzes
    vec4 lightPos[GROUND_MAX_LIGHTS];
    vec4 lightColor[GROUND_MAX_LIGHTS];
    vec4 lightSpot[GROUND_MAX_LIGHTS];
    vec4 lightSpot2[GROUND_MAX_LIGHTS];
    mat4 shadowMatrix;      // o MESMO da luz principal (common/shadow.glsl)
    vec4 shadowParams;
    vec4 shadowParams2;
    mat4 contactMatrix;     // uv do mapa de contato ← mundo
    vec4 contactParams;     // x = ligado, y..w livres
} g;

#endif

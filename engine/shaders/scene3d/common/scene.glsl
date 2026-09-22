// =============================================================================
//  Aurea / shaders / scene3d / common / scene.glsl
//
//  O bloco de parâmetros de um desenho 3D (cena + material) e as constantes
//  de iluminação. Espaço do mundo = espaço da composição: pixels, X para a
//  direita, Y PARA BAIXO, Z para dentro da tela. "Para cima" no mundo é −Y.
//
//  Cores lineares, pré-multiplicadas na saída (o compositor do Aurea).
// =============================================================================
#ifndef AUREA_SCENE_GLSL
#define AUREA_SCENE_GLSL

#include "../../common/bindings.glsl"

// Slots de textura do PBR.
#define TEX_BASE      AUREA_TEX0
#define TEX_MR        AUREA_TEX1
#define TEX_NORMAL    AUREA_TEX2
#define TEX_OCCLUSION AUREA_TEX3
#define TEX_EMISSIVE  AUREA_TEX4
#define TEX_IRRADIANCE AUREA_TEX5
#define TEX_PREFILTER AUREA_TEX6
#define TEX_BRDF      AUREA_TEX7
#define TEX_SHADOW    AUREA_TEX8

#define MAX_LIGHTS 4

layout(set = 0, binding = AUREA_PARAMS, std140) uniform SceneBlock {
    mat4 viewProj;          // clip ← mundo
    vec4 cameraPos;         // mundo; w = exposição (multiplicador linear)
    vec4 envParams;         // x = intensidade do ambiente, y = tem IBL (0/1),
                            // z = mips do prefiltrado, w = rotação do ambiente (rad)
    vec4 skyColor;          // ambiente analítico (sem IBL): céu
    vec4 groundColor;       //                               chão
    vec4 lightCount;        // x = luzes ativas
    vec4 lightPos[MAX_LIGHTS];    // xyz = posição (ponto/spot) ou direção PARA a luz (direcional); w = tipo (0 dir, 1 ponto, 2 spot)
    vec4 lightColor[MAX_LIGHTS];  // rgb * intensidade; w = alcance (0 = infinito)
    vec4 lightSpot[MAX_LIGHTS];   // xyz = direção do feixe; w = cos do cone externo
    vec4 lightSpot2[MAX_LIGHTS];  // x = cos do cone interno

    // Material
    vec4 baseColor;         // linear
    vec4 emissive;          // rgb já × força
    vec4 mr;                // metal, rugosidade, escala da normal, força da oclusão
    vec4 alpha;             // x = corte, y = modo (0 opaco, 1 máscara, 2 mistura), z = unlit, w = dupla face
    vec4 uvXform[5];        // offset.xy, escala.zw — base, mr, normal, oclusão, emissiva
    vec4 uvRot0;            // rotação (rad) de base, mr, normal, oclusão
    vec4 uvRot1;            // x = emissiva
    ivec4 uvSet0;           // conjunto de UV (0/1) de base, mr, normal, oclusão
    ivec4 uvSet1;           // x = emissiva
    ivec4 texMask0;         // tem textura: base, mr, normal, oclusão
    ivec4 texMask1;         // x = emissiva

    // Sombra da luz principal
    mat4 shadowMatrix;      // (uv, profundidade) do mapa ← mundo
    vec4 shadowParams;      // x = ligado, y = tamanho do texel, z = viés, w = índice da luz
} u;

const float PI = 3.14159265359;

#endif

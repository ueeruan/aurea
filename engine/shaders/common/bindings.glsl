// =============================================================================
//  Aurea / shaders / common / bindings.glsl
//
//  O layout universal de recursos — espelho de `binding::` em GPUBackend.hpp.
//
//  Todo shader do Aurea usa ESTES números. Um efeito novo não inventa layout:
//  ele declara só os slots que lê. Mudar um número aqui sem mudar o C++ quebra
//  na validação do Vulkan no primeiro draw — nunca silenciosamente.
// =============================================================================
#ifndef AUREA_BINDINGS_GLSL
#define AUREA_BINDINGS_GLSL

#define AUREA_TEX0    0
#define AUREA_TEX1    1
#define AUREA_TEX2    2
#define AUREA_TEX3    3
#define AUREA_TEX4    4
#define AUREA_TEX5    5
#define AUREA_TEX6    6
#define AUREA_TEX7    7
#define AUREA_TEX8    8
#define AUREA_TEX9    9
#define AUREA_TEX10   10
#define AUREA_TEX11   11
#define AUREA_PARAMS  12
#define AUREA_IMG0    13
#define AUREA_IMG1    14
#define AUREA_DATA    15

#endif

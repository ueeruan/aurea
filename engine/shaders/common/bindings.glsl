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
#define AUREA_PARAMS  4
#define AUREA_IMG0    5
#define AUREA_IMG1    6
#define AUREA_DATA    7

#endif

// Instancia o carregador de imagens do Assimp. O build enxuto desliga os
// exporters e o importador M3D, entao Common/Assimp.cpp nao cria estes
// simbolos; glTF/FBX ainda os usam para texturas embutidas.
#define STB_IMAGE_IMPLEMENTATION
#include "Common/StbCommon.h"

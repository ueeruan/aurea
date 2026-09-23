// =============================================================================
//  Aurea / render / ParticleScene.hpp  —  AUREA PARTICULAR no espaço da cena
//
//  A simulação continua ANALÍTICA (shaders/particles/particles.vert): a conta
//  dá a posição da partícula no espaço da CAMADA num instante. Este módulo
//  decide para onde esse ponto vai e com que dados:
//
//   - 2D de sempre: clip ← camada, sem buffer nenhum (bits = 0).
//   - Cena 3D: cada partícula vira um billboard voltado para a câmera, no
//     mundo (matriz da camada com a cadeia de pais), projetado pela MESMA
//     viewProj da cena e desenhado DENTRO do passe dela (profundidade de
//     verdade contra modelos e planos).
//   - Espaço mundo: a partícula fica onde NASCEU. A matriz da camada entra no
//     instante do nascimento, lida de um HISTÓRICO amostrado pelo C++ na
//     janela [t − vida máxima, t] em quadros INTEIROS do tempo local — a
//     amostra i depende só do tempo da timeline, nunca de quais quadros foram
//     vistos antes (prévia = export, seek ida e volta sem diferença).
//   - Emissão no nascimento: taxa, velocidade, direção, espalhamento, offset do
//     emissor e a velocidade herdada da camada saem do mesmo histórico, então
//     um keyframe não muda retroativamente quem já nasceu.
//   - Desfoque de movimento: subamostras de TEMPO do próprio shader (o mesmo
//     número de amostras e ângulo de obturador das camadas), média aditiva.
//
//  O histórico mora num storage buffer próprio (binding 16, AUREA_DATA1),
//  todas as camadas do quadro num buffer só (anel, como glifos e máscaras).
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <vector>

namespace aurea {

namespace particle_space {
/// Bits de `pc.mode.x` no shader (PS_* em particles.vert).
inline constexpr u32 kBuffer = 1u;   ///< há bloco no histórico
inline constexpr u32 k3D     = 2u;   ///< billboard no mundo 3D
inline constexpr u32 kWorld  = 4u;   ///< espaço mundo (matriz do nascimento)
inline constexpr u32 kMoving = 8u;   ///< matriz no instante do desenho (desfoque)
inline constexpr u32 kEmit   = 16u;  ///< emissão no instante do nascimento
/// Layout do bloco (em vec4): cabeçalho + amostras.
inline constexpr u32 kHeaderVec4 = 6;
inline constexpr u32 kEntryVec4 = 5;
/// Teto de amostras por camada: acima disso o passo vira 2, 3... quadros.
inline constexpr u32 kMaxSamples = 128;
}

/// Push constant do particles.vert (112 B, dentro dos 128 garantidos).
struct ParticlePush {
    Mat4 clip = Mat4::identity();   ///< 2D: clip ← camada/composição; 3D: clip ← mundo
    Vec4 mode{0, 0, 0, 1};          ///< bits, deslocamento de tempo (s), início do bloco, peso
    Vec4 camRight{1, 0, 0, 0};
    Vec4 camDown{0, 1, 0, 0};
};

/// Uma subamostra do desenho: o instante (deslocado do quadro) e, no 3D, a
/// câmera daquele instante.
struct ParticleSub {
    f32  shift = 0.0f;              ///< s, relativo ao tempo do quadro
    Mat4 clip = Mat4::identity();   ///< 3D: clip ← mundo no instante
    Vec4 right{1, 0, 0, 0};
    Vec4 down{0, 1, 0, 0};
};

/// O que o render precisa para pôr as partículas da camada no lugar certo.
/// Montado no prepare (sob o lock), junto do bloco de parâmetros.
struct ParticleSpace {
    u32  flags = 0;                 ///< particle_space::k*; 0 = o 2D de sempre
    u32  dataFirst = 0;             ///< início do bloco em FrameSnapshot::particleData (vec4)
    /// A textura da camada já está no espaço da COMPOSIÇÃO (compFromLayer =
    /// identidade): espaço mundo/desfoque no 2D e o 3D fora da cena.
    bool compSpace = false;
    /// Desfoque de movimento por subamostras de tempo (`subs` > 1).
    bool blur = false;
    f32  fps = 30.0f;
    std::vector<ParticleSub> subs;  ///< ≥ 1 quando flags != 0
};

} // namespace aurea

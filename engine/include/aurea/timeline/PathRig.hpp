// =============================================================================
//  Aurea / timeline / PathRig.hpp
//
//  Máscaras e rig do After Effects (lote M, gap_system itens 12, 23 e 25). O AE
//  é só a especificação de comportamento.
//
//  DADOS (moram na Mask e na Layer; gravados na seção MaskRig do projeto, que
//  só existe quando algum deles é usado: projeto antigo fica byte a byte igual):
//
//   - MaskFeatherPoint: ponto de feather de largura variável. O raio do feather
//     interpola ao longo da borda entre os pontos (render/MaskRaster).
//   - PathVertexDriver ("Points Follow Nulls"): o vértice do caminho segue o
//     ponto de âncora de outra camada (o nulo). Vale por cima do caminho
//     gravado e dos keys, ANTES da rasterização.
//   - PathFollow ("Nulls Follow Points" e "Trace Path"): a POSIÇÃO da camada
//     (o nulo) vem de um vértice de um caminho de outra camada, ou de um ponto
//     que anda pelo caminho (progresso 0..100 %, com laço opcional).
//
//  RESOLUÇÃO: quem avalia um caminho (mask::evaluate_path, vector::evaluate_group)
//  ou a matriz de uma camada (world::layer_matrix_frac) só tem a máscara, o
//  TrackSet ou a camada na mão. A composição dona é achada como as expressões
//  acham (expr::live_timelines: o Scope do quadro em render ou a timeline viva
//  do Engine). Sem dona (cópia solta, timeline que já morreu) vale o caminho /
//  a posição de sempre. Ciclo (o nulo segue o ponto que segue o nulo) também.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Handle.hpp"
#include "aurea/core/Math.hpp"

#include <vector>

namespace aurea {

struct Layer;
struct Mask;
struct MaskPoint;
class TrackSet;   // é `class` em Curve.hpp: no MSVC `struct` aqui muda o nome decorado e o link falha
struct VectorGroup;
struct BezierPath;
class Composition;

/// Ponto de feather de largura variável (a ferramenta Mask Feather do AE).
struct MaskFeatherPoint {
    u32  segment = 0;      ///< trecho do caminho (o que SAI do vértice `segment`)
    f32  t = 0.0f;         ///< 0..1 ao longo do trecho
    f32  radius = 0.0f;    ///< px da camada: > 0 para fora, < 0 para dentro
    f32  tension = 0.0f;   ///< 0 = o raio varia em linha reta até o vizinho, 1 = suave
    bool hold = false;     ///< o raio fica constante até o próximo ponto
    friend bool operator==(const MaskFeatherPoint&, const MaskFeatherPoint&) noexcept = default;
};
inline constexpr u32 kMaxFeatherPoints = 256;

/// "Points Follow Nulls": o vértice `vertex` segue a âncora da camada `driver`.
struct PathVertexDriver {
    u32     vertex = 0;
    LayerId driver{};
    friend bool operator==(const PathVertexDriver&, const PathVertexDriver&) noexcept = default;
};

/// O mesmo para um caminho livre da camada vetorial (grupo, caminho).
struct VectorPathDriver {
    u32     group = 0;
    u32     path = 0;
    u32     vertex = 0;
    LayerId driver{};
    friend bool operator==(const VectorPathDriver&, const VectorPathDriver&) noexcept = default;
};
inline constexpr u32 kMaxPathDrivers = 4096;

/// A posição da camada vem de um caminho de outra camada.
struct PathFollow {
    static constexpr u8 kNone = 0, kVertex = 1, kTrace = 2;
    static constexpr u8 kMask = 0, kVector = 1;
    u8      mode = kNone;
    u8      pathKind = kMask;
    LayerId source{};                 ///< a camada dona do caminho
    u32     a = 0;                    ///< id da máscara, ou o grupo
    u32     b = 0;                    ///< o caminho dentro do grupo (vetor)
    u32     vertex = 0;               ///< kVertex: o vértice seguido
    f32     progress = 0.0f;          ///< kTrace: 0..100 % do comprimento (sem o controle)
    /// kTrace: id do efeito "Controle deslizante" DESTA camada que anima o
    /// progresso (o pseudo-efeito Trace Path do AE). kInvalidIndex = `progress`.
    u32     progressEffect = kInvalidIndex;
    bool    loop = false;             ///< kTrace: depois de 100 % recomeça
    bool    orient = false;           ///< kTrace: a rotação soma a tangente do caminho
    friend bool operator==(const PathFollow&, const PathFollow&) noexcept = default;
};

namespace pathrig {

/// RotoBezier: tangentes automáticas a partir dos vizinhos (a posição dos
/// pontos não muda). `tension` por vértice (vazio ou curto = 1): 0 = canto,
/// 1 = a curva cheia. A direção é a da corda entre os vizinhos; o comprimento
/// é um terço do trecho de cada lado.
void roto_bezier(std::vector<MaskPoint>& points, bool closed, const std::vector<f32>& tension) noexcept;

/// "Points Follow Nulls" na máscara: troca a posição dos vértices ligados. As
/// tangentes ficam. Devolve se algum mudou.
bool apply_mask_drivers(const Mask& mask, f64 localFrame, std::vector<MaskPoint>& points) noexcept;

/// O mesmo nos caminhos do grupo JÁ avaliado (cópia) de uma camada vetorial:
/// o caminho ligado vira livre e parado na forma do instante. `tracks` = o
/// TrackSet da camada dona (é por ele que a camada é achada). Só custa quando
/// algum caminho vetorial do processo já foi ligado (`note_vector_drivers`).
void apply_vector_drivers(const TrackSet& tracks, u32 groupIndex, f64 localFrame, VectorGroup& evaluated) noexcept;
/// Quem cria ou abre uma ligação de caminho vetorial avisa (liga o gancho acima).
void note_vector_drivers() noexcept;

/// "Nulls Follow Points" / "Trace Path": a posição da camada no espaço do PAI
/// no instante local. Falso = sem ligação válida (vale a posição de sempre).
/// `tangentRad` (opcional) = direção do caminho no ponto, na composição
/// (só no Trace Path com `orient`; senão fica como entrou).
bool follow_position(const Layer& layer, f64 localFrame, Vec3& position, f32* tangentRad = nullptr) noexcept;

/// Caminho de uma camada no espaço da COMPOSIÇÃO no instante `compFrame`
/// (bezier com as ligações de vértice aplicadas). Falso = caminho não existe.
bool comp_path(const Composition& comp, const Layer& owner, u8 pathKind, u32 a, u32 b, f64 compFrame,
               BezierPath& out) noexcept;

/// Ponto a `fraction` (0..1) do COMPRIMENTO do caminho e a tangente unitária.
bool point_on_path(const BezierPath& path, f64 fraction, Vec2& point, Vec2& tangent) noexcept;

} // namespace pathrig

/// Auto-trace (Layer > Auto-trace do AE): do canal de uma imagem para caminhos
/// bezier fechados. Função pura (sem motor): quem tem os pixels chama.
namespace masktrace {

struct Options {
    u32  channel = 0;              ///< 0 alfa, 1 luminância, 2 vermelho, 3 verde, 4 azul
    f32  threshold = 0.5f;         ///< 0..1: acima dele é "dentro"
    f32  tolerance = 1.0f;         ///< px de SAÍDA: quanto a curva pode fugir do contorno
    f32  minArea = 10.0f;          ///< px² de SAÍDA: contornos menores ficam de fora
    f32  cornerRoundness = 50.0f;  ///< 0..100 %: suaviza o canal antes do limiar (até 6 px em 100 %); 0 = cantos vivos
    f32  blur = 1.0f;              ///< px da imagem: desfoque antes de traçar (0 = nenhum)
    bool invert = false;           ///< traça o que está ABAIXO do limiar
};

struct Traced {
    std::vector<MaskPoint> points; ///< caminho fechado
    bool hole = false;             ///< contorno interno (furo): entra como Subtrair
    f32  area = 0.0f;              ///< px² (já na escala de saída)
};

/// Contornos do canal de `rgba` (w×h, RGBA8, alfa reto), do maior para o
/// menor, em px da imagem × `scale` (px da camada). `maxContours` corta os
/// menores. Falso = nada acima do limiar.
bool trace(const u8* rgba, u32 width, u32 height, const Options& options, Vec2 scale, u32 maxContours,
           std::vector<Traced>& out);

} // namespace masktrace
} // namespace aurea

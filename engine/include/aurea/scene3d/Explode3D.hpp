#pragma once
// =============================================================================
//  Aurea / scene3d / Explode3D.hpp — efeito "Explosão" (aurea.3d.explode)
//
//  O objeto 3D (modelo importado, forma 3D, texto 3D) quebra em PEÇAS da
//  própria malha que voam a partir de um ponto de impacto: giro, gravidade,
//  arrasto, onda pela distância ao impacto, encolher/esmaecer no fim, brilho
//  quente nas faces de quebra e o modo "Montar" (o mesmo voo ao contrário).
//
//  Como funciona (tudo no motor, igual nas duas plataformas):
//    1. FRATURA (CPU, uma vez por malha/peças/semente/espessura, em cache):
//       cada nó com malha é subdividido até ter triângulos para as peças e
//       os triângulos são agrupados em células de Voronoi pelos centróides
//       (sementes sorteadas pela área, uma relaxação de Lloyd). Cada peça vira
//       um NÓ do asset derivado com a parte da superfície original (mesmo
//       material, UV, normais, cores) e uma TAMPA que fecha a peça até um
//       ápice dentro do objeto (a "espessura"), num material de interior
//       (escuro, emissivo nas bordas da quebra por um degradê em UV1).
//    2. MOVIMENTO (fechado, determinístico: só parâmetros + instante + semente;
//       prévia = export, pular no tempo funciona): place_model avalia a pose
//       normal (animação, layout do texto, animadores) e depois cada peça
//       segue o nó de origem e recebe o voo dela (explode_apply).
//  Progresso 0 (ou efeito desligado) desenha o asset ORIGINAL: imagem igual.
//
//  Referências de comportamento: os efeitos de explosão/fratura dos editores
//  de motion design (descritos em vídeo). Implementação própria.
// =============================================================================
#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <memory>
#include <vector>

namespace aurea {
class ParameterRegistry;
struct Layer;
struct EffectInstance;
struct MaterialOverride;
enum class DeviceTier : u8;
}

namespace aurea::scene3d {

struct SceneAsset;

namespace explode3d {

/// Índices dos parâmetros (fixos nos projetos salvos: só acrescentar no fim).
enum : u32 {
    kProgress = 0, kTiming, kStart, kDuration, kPieces, kSeed, kThickness, kImpact, kDirection, kForce,
    kSpread, kVelocityRandom, kSpin, kAxisRandom, kGravity, kDrag, kWave, kEndScale, kFade, kReassemble,
    kGlowColor, kGlow, kInteriorDark, kParamCount
};
enum : u32 { kTimingProgress = 0, kTimingAuto = 1 };

inline constexpr u32 kMinPieces = 8;
inline constexpr u32 kMaxPieces = 2000;

/// Declaração única (o efeito e a avaliação do renderer usam a mesma).
void declare_parameters(ParameterRegistry& p);
/// A declaração, construída uma vez.
[[nodiscard]] const ParameterRegistry& parameters();

/// Teto de peças pela classe do aparelho (Engine::initialize informa; padrão
/// = o maior). LOW 250, MID 600, HIGH 1200, ULTRA 2000.
void set_device_tier(DeviceTier tier) noexcept;
[[nodiscard]] u32 piece_cap() noexcept;
[[nodiscard]] u32 piece_cap_for(DeviceTier tier) noexcept;

/// O que muda a GEOMETRIA da fratura (o resto é só movimento/cor por quadro).
struct FractureSpec {
    u32 pieces = 120;
    u32 seed = 0;
    f32 thickness = 0.6f;   ///< 0..1 (0 = casca oca, sem tampa)
    [[nodiscard]] bool operator==(const FractureSpec&) const noexcept = default;
};

/// O asset fraturado. `asset` é um SceneAsset normal (o renderer desenha como
/// qualquer modelo): os nós 0..firstFragment-1 são os do original com malhas
/// vazias (a hierarquia, animação e layout de texto continuam valendo); a
/// partir de `firstFragment`, uma peça por nó (`Node::parent` = nó de origem,
/// −1 = peça já no espaço da cena; `Node::translation` = centróide no espaço
/// do nó de origem; a malha fica em volta do centróide).
struct Fractured {
    const SceneAsset* asset = nullptr;   ///< vive junto deste objeto (asset_of)
    u32 firstFragment = 0;
    u32 fragments = 0;
    u32 baseMaterials = 0;       ///< materiais do original; os de interior vêm depois
    std::vector<i32> interiorOf; ///< por material de interior: o material de origem (−1 = padrão)
    std::vector<u32> source;     ///< por peça: o nó de origem (opacidade/unidades do texto)
    u64 key = 0;                 ///< chave do modelo na GPU (estável para a mesma fratura)
};

/// Fratura (com cache). Nulo = o asset não fratura (sem malha, nuvem...).
[[nodiscard]] std::shared_ptr<const Fractured> fracture(const std::shared_ptr<const SceneAsset>& source,
                                                        u64 sourceKey, const FractureSpec& spec);
/// O asset da fratura, com a mesma posse (o desenho segura a fratura inteira).
[[nodiscard]] inline std::shared_ptr<const SceneAsset> asset_of(const std::shared_ptr<const Fractured>& f) noexcept {
    return f ? std::shared_ptr<const SceneAsset>(f, f->asset) : nullptr;
}
/// O asset é um asset fraturado deste módulo? (os dados ficam junto dele.)
[[nodiscard]] const Fractured* fractured_of(const SceneAsset& asset) noexcept;
/// Esvazia o cache (testes; pressão de memória).
void clear_cache() noexcept;

/// Efeito "Explosão" ligado na camada (o primeiro, se houver mais de um).
[[nodiscard]] const EffectInstance* find(const Layer& layer) noexcept;

/// Valores avaliados no instante (tempo local da camada, em quadros).
struct Values {
    f32 progress = 0.0f;   ///< 0..1 já com Tempo automático e Montar
    FractureSpec spec{};
    Vec3 impact{0, 0, 0};  ///< −1..1 da meia caixa
    Vec3 direction{0, 0, -1};
    f32 force = 1.0f, spread = 0.75f, velocityRandom = 0.4f, spin = 6.2831853f, axisRandom = 1.0f;
    f32 gravity = 0.4f, drag = 0.2f, wave = 0.35f, endScale = 1.0f, fade = 0.0f;
    Vec3 glowColor{1.0f, 0.25f, 0.03f};   ///< linear
    f32 glow = 0.6f, interiorDark = 0.6f;
};
[[nodiscard]] Values evaluate(const Layer& layer, const EffectInstance& effect, f64 localFrames, f64 fps) noexcept;

/// O que o Renderer precisa no quadro: explode agora? qual asset?
struct Active {
    bool exploding = false;
    Values values{};
    std::shared_ptr<const Fractured> fractured;
};
/// Para o modelo `source` da camada no instante: nulo/exploding falso = desenha
/// o original (progresso 0, efeito desligado ou asset que não fratura).
[[nodiscard]] Active resolve(const Layer& layer, const std::shared_ptr<const SceneAsset>& source, u64 sourceKey,
                             f64 localFrames, f64 fps);

/// Movimento das peças: `nodeWorld`/`nodeOpacity` já com a pose do quadro (os
/// nós originais); escreve os das peças. `world` = mundo ← cena do modelo (a
/// gravidade e a direção são do MUNDO). `materials` recebe o interior/brilho.
void apply(const SceneAsset& asset, const Layer& layer, f64 localFrames, f64 fps, const Mat4& world,
           std::vector<Mat4>& nodeWorld, std::vector<f32>& nodeOpacity, std::vector<MaterialOverride>& materials);

/// Só a conta de UMA peça (testes): matriz delta no espaço da cena para a
/// peça `index` com centróide `center`, e a opacidade/escala que ela leva.
struct PieceMotion { Mat4 delta = Mat4::identity(); f32 opacity = 1.0f; f32 scale = 1.0f; };
[[nodiscard]] PieceMotion piece_motion(const Values& v, u32 index, Vec3 center, Vec3 impactPoint, f32 impactReach,
                                       f32 size, Vec3 direction, Vec3 down) noexcept;

} // namespace explode3d
} // namespace aurea::scene3d

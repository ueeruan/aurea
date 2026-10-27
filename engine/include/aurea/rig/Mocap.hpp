// =============================================================================
//  Aurea / rig / Mocap.hpp
//
//  Motion Capture (BETA, 0.3.6): o vídeo vira animação do rig humanoide.
//
//  As PLATAFORMAS detectam a pose no aparelho (Android: ML Kit Pose Detection;
//  iOS: Vision) e entregam uma sequência de PoseFrame na ordem do modelo de 33
//  pontos de corpo. O MOTOR (aqui, sem estado global e sem GPU) faz o resto:
//
//   1. limpeza: interpolação de lacunas curtas (ponto com confiança baixa ou
//      ausente), filtro One Euro por coordenada (passada para frente e para
//      trás: sem atraso), mais forte onde a confiança é baixa; detecção de
//      baixa confiança por membro (partes ocultas);
//   2. 3D: com pontos de mundo (metros) usa direto; sem eles, levanta o 2D com
//      comprimentos de osso constantes (o maior comprimento visto na imagem) e
//      o sinal da profundidade relativa do detector (método de Taylor);
//   3. rotações: para cada osso canônico, a rotação de MUNDO desde a T-pose
//      (quadril/peito/cabeça por referencial; membros por mira + giro pelo
//      plano da dobra; limites de articulação no cotovelo/joelho);
//   4. intensidade, translação do quadril (escala pela altura da perna:
//      proporções diferentes), alinhamento (giro e altura do chão);
//   5. foot lock (pé parado e perto do chão fica preso) e IK de dois ossos
//      de mãos e pés; o resultado continua sendo rotação por osso;
//   6. rotação local por osso (rig/Humanoid.hpp: offset_from_world_delta) →
//      keys BonePose (BoneKey) com redução de keys, ou a prévia transitória.
//
//  Tudo em lotes, com progresso e cancelamento (atômicos que a UI lê/escreve).
//  Vídeo comum não tem profundidade precisa: o resultado é uma aproximação.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/rig/Humanoid.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace aurea::rig {

// ---- Entrada (contrato BRIEF-MOCAP) ---------------------------------------------

inline constexpr u32 kPoseLandmarks = 33;

/// x, y normalizados na imagem (0..1, x para a direita, y para BAIXO); z é a
/// profundidade relativa na escala de x (maior = mais longe da câmera). Com
/// `has_world`, x/y/z são metros na mesma orientação (y para baixo, z longe).
struct PoseLandmark {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float confidence = 0.0f;   ///< 0..1 (0 = ausente)
};

struct PoseFrame {
    double time_s = 0.0;
    bool has_world = false;
    std::array<PoseLandmark, kPoseLandmarks> pts{};
};

/// Índices do modelo de 33 pontos (lado = do corpo da pessoa).
namespace lm {
enum : u32 {
    Nose = 0, LeftEyeInner, LeftEye, LeftEyeOuter, RightEyeInner, RightEye, RightEyeOuter,
    LeftEar = 7, RightEar = 8, MouthLeft = 9, MouthRight = 10,
    LeftShoulder = 11, RightShoulder = 12, LeftElbow = 13, RightElbow = 14, LeftWrist = 15, RightWrist = 16,
    LeftPinky = 17, RightPinky = 18, LeftIndex = 19, RightIndex = 20, LeftThumb = 21, RightThumb = 22,
    LeftHip = 23, RightHip = 24, LeftKnee = 25, RightKnee = 26, LeftAnkle = 27, RightAnkle = 28,
    LeftHeel = 29, RightHeel = 30, LeftFootIndex = 31, RightFootIndex = 32,
};
} // namespace lm

/// Quadro empacotado das pontes (JNI / Objective-C):
/// [tempo s, tem mundo 0/1, 33 × (x, y, z, confiança)].
inline constexpr u32 kMocapFrameFloats = 2 + kPoseLandmarks * 4;

/// Desempacota `count` quadros (ignora quadros com números inválidos).
void unpack_frames(const f32* data, u32 count, std::vector<PoseFrame>& out);

// ---- Parâmetros e relatório -------------------------------------------------------

enum class Limb : u8 { Torso = 0, Head, LeftArm, RightArm, LeftLeg, RightLeg, Count };
inline constexpr usize kLimbCount = static_cast<usize>(Limb::Count);

struct MocapParams {
    f32 intensity = 1.0f;     ///< 0..1.5: quanto do movimento (1 = original)
    f32 smoothing = 0.5f;     ///< 0..1: remove tremor (0 = cru, 1 = bem suave)
    f32 speed = 1.0f;         ///< 0.25..4: velocidade na timeline
    f32 yawDeg = 0.0f;        ///< alinhamento: giro em volta do eixo vertical
    f32 floorOffset = 0.0f;   ///< alinhamento: altura do chão (fração da perna, −0.5..0.5)
    bool footLock = true;     ///< pé parado no chão fica preso
    bool footIk = true;       ///< pés não atravessam o chão
    bool handIk = true;       ///< mãos miram o alvo proporcional ao braço
    bool rootMotion = true;   ///< translação do quadril
    f32 minConfidence = 0.5f; ///< abaixo disso o ponto conta como ausente
    f32 maxGapSeconds = 0.6f; ///< lacunas até isso são interpoladas
    f32 imageAspect = 1.0f;   ///< largura/altura do vídeo (pontos 2D)

    [[nodiscard]] MocapParams sanitized() const noexcept;
};

struct MocapReport {
    u32 frames = 0;
    f64 duration = 0.0;
    bool hasWorld = false;
    std::array<f32, kLimbCount> lowFraction{};   ///< fração de quadros com o membro oculto/incerto
    std::array<u8, kLimbCount> missing{};        ///< membro nunca visto (fica em repouso)
    bool lowConfidence = false;                  ///< algum membro > 25% incerto (aviso na UI)
    f32 footLockedFraction = 0.0f;
    f32 interpolatedFraction = 0.0f;             ///< pontos preenchidos por interpolação
};

/// Progresso e cancelamento (a UI escreve `cancel`, lê `progress` 0..1).
struct MocapControl {
    std::atomic<bool>* cancel = nullptr;
    std::atomic<f32>* progress = nullptr;
    f32 base = 0.0f, span = 1.0f;   ///< faixa desta etapa no progresso total

    [[nodiscard]] bool cancelled() const noexcept { return cancel && cancel->load(std::memory_order_relaxed); }
    void report(f32 fraction) const noexcept {
        if (progress) progress->store(base + span * (fraction < 0 ? 0 : fraction > 1 ? 1 : fraction), std::memory_order_relaxed);
    }
};

// ---- Peças do pipeline (expostas para os testes) ---------------------------------------

/// Filtro One Euro (Casiez et al. 2012) de um escalar.
class OneEuro {
public:
    OneEuro(f64 minCutoff, f64 beta, f64 dCutoff = 1.0) noexcept : minCutoff_(minCutoff), beta_(beta), dCutoff_(dCutoff) {}
    /// `cutoffScale` < 1 suaviza mais (confiança baixa).
    f64 filter(f64 x, f64 dt, f64 cutoffScale = 1.0) noexcept;
    void reset() noexcept { started_ = false; }

private:
    static f64 alpha(f64 cutoff, f64 dt) noexcept;
    f64 minCutoff_, beta_, dCutoff_;
    f64 x_ = 0.0, dx_ = 0.0;
    bool started_ = false;
};

/// Ordena por tempo, interpola lacunas, suaviza (One Euro ida e volta) e mede
/// a confiança por membro. `out` tem os mesmos quadros (confiança 0 = ponto
/// segurado de um vizinho porque a lacuna era longa demais).
bool clean_frames(const std::vector<PoseFrame>& in, const MocapParams& params, std::vector<PoseFrame>& out,
                  MocapReport& report, const MocapControl& control = {});

/// Pontos 3D em espaço de CAPTURA (x = esquerda da pessoa de frente para a
/// câmera, y para cima, z para a câmera), 33 + meio do quadril (33) + meio
/// dos ombros (34).
inline constexpr u32 kCapturePoints = kPoseLandmarks + 2;
inline constexpr u32 kMidHip = kPoseLandmarks, kMidShoulder = kPoseLandmarks + 1;
using CapturePose = std::array<Vec3, kCapturePoints>;

/// Levanta para 3D com comprimentos de osso constantes (ou normaliza os de
/// mundo). Devolve o comprimento médio da perna (quadril→tornozelo).
f32 lift_frames(const std::vector<PoseFrame>& frames, const MocapParams& params, std::vector<CapturePose>& out,
                const MocapControl& control = {});

/// Resultado do solver por quadro de captura.
struct MocapResult {
    std::vector<f64> times;                                     ///< s, desde 0
    std::vector<std::array<Quat, kHumanoidBoneCount>> local;    ///< params 0..2 (quaternion) por osso
    std::vector<Vec3> hipsLocal;                                ///< params 3..5 do quadril
    std::vector<std::array<Quat, kHumanoidBoneCount>> world;    ///< delta de mundo desde a T-pose
    std::vector<std::array<u8, 2>> footLocked;                  ///< esquerdo, direito
    MocapReport report;
    f32 legLength = 0.0f;                                       ///< do modelo (unidades do modelo)
};

/// Referencial do modelo deduzido da T-pose: esquerda, cima, frente.
struct ModelBasis {
    Vec3 left{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};
};
[[nodiscard]] ModelBasis model_basis(const HumanoidRest& rest) noexcept;

/// Pipeline inteiro. false = cancelado ou sem quadros úteis.
bool solve(const std::vector<PoseFrame>& frames, const HumanoidRest& rest, const MocapParams& params,
           MocapResult& out, const MocapControl& control = {});

// ---- Saída na timeline ------------------------------------------------------------

/// Quadros da timeline: `local[k]` e `hips[k]` para o quadro local start+k.
struct MocapTimeline {
    i64 startFrame = 0;
    std::vector<std::array<Quat, kHumanoidBoneCount>> local;
    std::vector<Vec3> hips;
};

/// Reamostra o resultado na cadência da composição com a velocidade.
void resample(const MocapResult& result, const MocapParams& params, f64 fps, i64 startFrame, MocapTimeline& out);

/// Keys de osso (só os ossos mapeados), com redução: um quadro sai quando a
/// curva linear entre os que ficam passa a no máximo `toleranceDeg` graus (e
/// 0,2% da perna na translação) da original.
void bake_keys(const MocapTimeline& timeline, const HumanoidMap& map, const HumanoidRest& rest,
               bool rootMotion, f32 toleranceDeg, std::vector<BoneKey>& out);

/// Redução de keys de uma curva de vários canais (Ramer–Douglas–Peucker pelo
/// maior desvio por canal). `keep[i]` = 1 nos quadros que ficam.
void reduce_keys(const std::vector<f32>& values, u32 channels, const std::vector<f32>& tolerance,
                 std::vector<u8>& keep);

// ---- Prévia transitória (não salva, não entra no desfazer) ----------------------------

/// O personagem tocando a captura no preview sem gravar keys: a camada leva
/// um ponteiro para isto (Model3DData::mocapPreview) e scene3d::bone_offsets
/// usa estes valores no lugar das trilhas BonePose dos nós mapeados.
struct MocapPreview {
    i64 startFrame = 0;
    std::vector<i32> nodes;      ///< nós mapeados (ordem das colunas)
    std::vector<Quat> rotation;  ///< quadros × nodes
    i32 hipsNode = -1;
    std::vector<Vec3> hips;      ///< por quadro (local do pai do quadril)
    [[nodiscard]] u32 frames() const noexcept { return nodes.empty() ? 0u : static_cast<u32>(rotation.size() / nodes.size()); }
    /// Rotação (quaternion) e translação no quadro local fracionário.
    void sample(f64 local, usize column, Quat& rot) const noexcept;
    [[nodiscard]] Vec3 sample_hips(f64 local) const noexcept;
};

[[nodiscard]] MocapPreview make_preview(const MocapTimeline& timeline, const HumanoidMap& map, bool rootMotion);

/// Só a prévia ATIVA vale (uma por processo): uma cópia velha que o desfazer
/// traga de volta dentro de um snapshot da composição fica inerte.
void set_active_preview(const MocapPreview* preview) noexcept;
[[nodiscard]] const MocapPreview* active_preview(const std::shared_ptr<const MocapPreview>& candidate) noexcept;

} // namespace aurea::rig

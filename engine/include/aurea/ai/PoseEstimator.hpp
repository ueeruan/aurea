// =============================================================================
//  Aurea / ai / PoseEstimator.hpp
//
//  Motion Capture do Aurea PC: a detecção de pose que no celular é do sistema
//  (Android: ML Kit; iOS: Vision) roda aqui no ncnn, com os modelos BlazePose
//  GHUM 3D do MediaPipe (Google, Apache-2.0) na NOSSA conversão — origem,
//  receita e hashes em engine/assets/mocap/README.md.
//
//  SÓ DESKTOP (Windows): o arquivo entra no aurea_core apenas quando o alvo é
//  o Windows (cmake/AureaNcnn.cmake); Android e iOS nem compilam.
//
//  O fluxo é o do MediaPipe no modo vídeo:
//   1. detector (224×224, quadro inteiro com letterbox) → a pessoa mais
//      provável e dois pontos de alinhamento (meio do quadril e o ponto que
//      dá o tamanho do corpo) → ROI quadrada e girada;
//   2. landmarks (256×256 recortado da ROI) → 39 pontos: os 33 do corpo, no
//      MESMO formato do ML Kit que o MocapDetector.kt manda ao motor, e os
//      dois auxiliares que dão a ROI do quadro seguinte (rastreio);
//   3. o detector só volta quando a pessoa some (presença < 0,5).
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/rig/Mocap.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace aurea::mocap {

/// 33 pontos no formato do ML Kit (rig::PoseLandmark): x, y normalizados na
/// imagem (y para baixo), z na escala de x (maior = mais longe), confiança =
/// probabilidade de o ponto estar no quadro (0 = sem pessoa).
using Landmarks = std::array<rig::PoseLandmark, rig::kPoseLandmarks>;

/// Região da pessoa no quadro, em PIXELS: quadrado de lado `size` centrado em
/// (cx, cy), girado por `rotation` radianos (0 = pessoa em pé; positivo gira
/// no sentido horário da tela, y para baixo). Vazia (size 0) = sem ROI.
struct PoseRoi {
    f32 cx = 0.0f, cy = 0.0f;
    f32 size = 0.0f;
    f32 rotation = 0.0f;
    [[nodiscard]] bool valid() const noexcept;
};

// ---- Contas da ROI (puras; testadas sem modelo) ------------------------------

/// AlignmentPointsRects + RectTransformation do MediaPipe: centro em (x0, y0)
/// (meio do quadril), lado 2·|p1 − p0|·`scale` (p1 = ponto do tamanho do
/// corpo) e o giro que deixa p0 → p1 apontando para CIMA no recorte.
[[nodiscard]] PoseRoi roi_from_alignment(f32 x0, f32 y0, f32 x1, f32 y1, f32 scale = 1.25f) noexcept;
/// Ponto (u, v) do recorte (0..1, y para baixo) → pixel da imagem.
void roi_to_image(const PoseRoi& roi, f32 u, f32 v, f32& x, f32& y) noexcept;
/// Pixel da imagem → ponto (u, v) do recorte (o inverso de roi_to_image).
void image_to_roi(const PoseRoi& roi, f32 x, f32 y, f32& u, f32& v) noexcept;
/// Reserva quando os pontos auxiliares não servem: quadrado sem giro em volta
/// da caixa dos pontos com confiança >= `minConfidence`, lado × `margin`.
/// Vazia com menos de 4 pontos.
[[nodiscard]] PoseRoi roi_from_landmarks(const Landmarks& pts, u32 width, u32 height,
                                         f32 minConfidence = 0.5f, f32 margin = 1.25f) noexcept;

/// Imagem w×h dentro de um quadrado `size` (proporção mantida, centrada):
/// pixel da imagem × scale + pad = pixel do quadrado.
struct Letterbox {
    f32 scale = 1.0f, padX = 0.0f, padY = 0.0f;
};
[[nodiscard]] Letterbox letterbox(u32 width, u32 height, u32 size) noexcept;

/// Âncoras SSD do detector (SsdAnchorsCalculator do MediaPipe: 224², passos
/// 8/16/32/32/32, tamanho fixo): centros normalizados, na ordem das saídas.
[[nodiscard]] u32 detector_anchor_count() noexcept;
void detector_anchor(u32 index, f32& cx, f32& cy) noexcept;

/// O quadro das pontes (rig::kMocapFrameFloats): tempo, sem mundo (0) e
/// 33 × (x, y, z, confiança) — o mesmo do MocapDetector.kt.
void pack_frame(f64 timeSeconds, const Landmarks& pts, f32* out) noexcept;

// ---- Imagem com pirâmide -----------------------------------------------------

/// Quadro RGBA8 com níveis de metade em metade (média 2×2). O recorte de
/// 256 px de uma pessoa grande num vídeo 1080p reduz 4–5×: amostrar o nível
/// certo (e 2×2 dentro dele) evita o serrilhado que fazia os pontos tremerem.
struct PoseImage {
    struct Level {
        const u8* rgba = nullptr;
        u32 width = 0, height = 0, stride = 0;   ///< stride em bytes
    };
    std::vector<Level> levels;                   ///< [0] = imagem da análise

    /// Só o nível 0, sem copiar (a memória é de quem chama).
    void wrap(const u8* rgba, u32 width, u32 height, u32 stride);
    /// Nível 0 = `rgba` (sem copiar) e até `maxLevels` reduções guardadas aqui.
    void build(const u8* rgba, u32 width, u32 height, u32 stride, u32 maxLevels = 4);
    [[nodiscard]] bool valid() const noexcept { return !levels.empty() && levels[0].rgba && levels[0].width && levels[0].height; }
    [[nodiscard]] u32 width() const noexcept { return levels.empty() ? 0u : levels[0].width; }
    [[nodiscard]] u32 height() const noexcept { return levels.empty() ? 0u : levels[0].height; }

private:
    std::vector<std::vector<u8>> storage_;
};

// ---- Saída completa (PC) --------------------------------------------------------

/// Tudo o que o modelo de landmarks dá num quadro.
struct PoseOutput {
    Landmarks image{};                                ///< formato do ML Kit (confiança = presença no quadro)
    std::array<f32, rig::kPoseLandmarks> visibility{};///< 0..1: o ponto está visível (não encoberto)
    /// Pontos de MUNDO do BlazePose GHUM 3D: metros, origem no meio do quadril,
    /// x para a direita e y para baixo NA IMAGEM (já desfeito o giro da ROI),
    /// z maior = mais longe da câmera.
    std::array<Vec3, rig::kPoseLandmarks> world{};
    f32 presence = 0.0f;
    f32 aux[4]{};   ///< pixels: meio do quadril (x, y) e ponto do tamanho do corpo (x, y)
};

/// Modelo de landmarks: o "full" (6 MB, ~3 ms) ou o "heavy" (27 MB, mais preciso).
enum class LandmarkModel : u8 { Full = 0, Heavy = 1 };

// ---- Rede --------------------------------------------------------------------

/// As duas redes (detector e landmarks). Uma instância pertence a UMA thread.
class PoseEstimator {
public:
    enum class Backend : u8 { Auto, Cpu, Vulkan };
    static constexpr u32 kDetectorSize = 224;
    static constexpr u32 kLandmarkSize = 256;
    static constexpr f32 kMinDetection = 0.5f;   ///< min_score_thresh do detector
    static constexpr f32 kMinPresence = 0.5f;    ///< presença da pessoa (ThresholdingCalculator)

    PoseEstimator() noexcept;
    ~PoseEstimator();
    PoseEstimator(const PoseEstimator&) = delete;
    PoseEstimator& operator=(const PoseEstimator&) = delete;

    /// Lê pose_detection.{param,bin} e pose_landmark_full.{param,bin} (ou
    /// _heavy, com `model` Heavy; sem os arquivos do heavy cai no full) de
    /// `modelDir` (UTF-8). Auto = Vulkan quando há GPU de verdade, senão CPU.
    /// `cpuThreads` 0 = metade dos núcleos, no máximo 4.
    [[nodiscard]] Status load(const std::string& modelDir, Backend backend = Backend::Auto, u32 cpuThreads = 0,
                              LandmarkModel model = LandmarkModel::Full);
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] Backend backend() const noexcept;
    [[nodiscard]] LandmarkModel model() const noexcept;
    /// Refino de x/y pelo heatmap (RefineLandmarksFromHeatmap do MediaPipe,
    /// janela 7; só com o heavy, o único convertido com o heatmap) e recorte
    /// sem serrilhado (pirâmide + 2×2). Os dois começam desligados (o mesmo
    /// resultado de antes nas chamadas com ponteiro).
    void set_refine(bool heatmap, bool antialias) noexcept;

    /// Detector no quadro inteiro (RGBA8, `stride` em bytes): a pessoa mais
    /// provável (NMS ponderado) vira a ROI dos landmarks. false = ninguém
    /// acima de kMinDetection.
    [[nodiscard]] bool detect(const u8* rgba, u32 width, u32 height, u32 stride, PoseRoi& roi, f32* score = nullptr);
    /// `prefer` (opcional): a ROI rastreada — entre as pessoas achadas, a que
    /// bate com ela ganha da mais provável (não troca de pessoa no meio do vídeo).
    [[nodiscard]] bool detect(const PoseImage& image, PoseRoi& roi, f32* score = nullptr, const PoseRoi* prefer = nullptr);

    /// Os pontos dentro de `roi`. Devolve a presença da pessoa (0..1); abaixo
    /// de kMinPresence `out` sai zerado. `next` (opcional): a ROI do próximo
    /// quadro pelos pontos auxiliares (vazia se não servir).
    f32 landmarks(const u8* rgba, u32 width, u32 height, u32 stride, const PoseRoi& roi,
                  Landmarks& out, PoseRoi* next = nullptr);
    /// O mesmo com a saída completa (mundo, visibilidade, pontos auxiliares).
    f32 landmarks(const PoseImage& image, const PoseRoi& roi, PoseOutput& out);

    /// Tempo de rede (detector + landmarks) da última chamada a estimate_pose, ms.
    [[nodiscard]] f32 last_ms() const noexcept;
    void add_ms(f32 ms) noexcept;
    void reset_ms() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Um quadro: `roi` vazia → detector; senão landmarks na ROI (e o detector de
/// novo se a pessoa sumiu dela). Na volta, `roi` é a do próximo quadro (vazia
/// se ninguém foi achado) e `out` tem os 33 pontos (confiança 0 sem pessoa).
/// `detected` (opcional) diz se o detector rodou neste quadro.
bool estimate_pose(PoseEstimator& estimator, const u8* rgba, u32 width, u32 height, u32 stride,
                   PoseRoi& roi, Landmarks& out, bool* detected = nullptr);

/// O rastreio de um vídeo: a ROI de um quadro para o outro.
class PoseTracker {
public:
    explicit PoseTracker(PoseEstimator& estimator) noexcept : estimator_(estimator) {}
    /// 33 pontos do quadro; false = ninguém (pontos com confiança 0).
    bool track(const u8* rgba, u32 width, u32 height, u32 stride, Landmarks& out);
    void reset() noexcept { roi_ = {}; }
    [[nodiscard]] const PoseRoi& roi() const noexcept { return roi_; }
    [[nodiscard]] u32 detector_runs() const noexcept { return detections_; }

private:
    PoseEstimator& estimator_;
    PoseRoi roi_{};
    u32 detections_ = 0;
};

// ---- Rastreio de vídeo do PC (qualidade) ------------------------------------------

/// Filtro One Euro de um escalar (Casiez 2012), ida só (tempo real).
struct OneEuro1 {
    f32 minCutoff = 1.0f, beta = 0.0f, dCutoff = 1.0f;
    f32 x = 0.0f, dx = 0.0f;
    bool started = false;
    /// `speedScale` converte a velocidade para a unidade de `beta` (ex.: 1/tamanho do corpo).
    f32 filter(f32 value, f32 dt, f32 speedScale = 1.0f) noexcept;
};

/// O rastreio do Motion Capture do PC, com o que o MediaPipe faz no modo
/// vídeo e o PoseTracker não fazia:
///  - a ROI do quadro seguinte sai dos pontos auxiliares FILTRADOS (One Euro,
///    como o PoseLandmarkFiltering do MediaPipe): o recorte não treme, os
///    pontos também não;
///  - o detector volta quando a pessoa some, quando ela encosta na borda do
///    recorte (ROI atrasada num movimento rápido) e a cada `recheckEvery`
///    quadros, para conferir se a ROI não grudou em outra coisa;
///  - troca de lados (esquerda ↔ direita) num quadro só, por membro: o
///    modelo às vezes inverte as pernas de perfil; aqui volta ao lado certo.
struct VideoTrackerOptions {
    bool filterRoi = true;
    bool fixSwaps = true;
    bool borderRedetect = true;
    u32 recheckEvery = 15;   ///< 0 = nunca
};

class VideoPoseTracker {
public:
    VideoPoseTracker(PoseEstimator& estimator, const VideoTrackerOptions& options) noexcept
        : estimator_(estimator), options_(options) {}
    /// Um quadro (`dt` = segundos desde o anterior). false = ninguém (saída zerada).
    bool track(const PoseImage& image, f32 dt, PoseOutput& out);
    void reset() noexcept;
    [[nodiscard]] u32 detector_runs() const noexcept { return detections_; }
    [[nodiscard]] u32 swaps_fixed() const noexcept { return swaps_; }
    [[nodiscard]] const PoseRoi& roi() const noexcept { return roi_; }

private:
    bool run(const PoseImage& image, const PoseRoi& roi, PoseOutput& out, f32& presence);
    PoseRoi roi_from_aux(const PoseOutput& o, f32 dt, u32 width, u32 height);
    void fix_swaps(PoseOutput& o);

    PoseEstimator& estimator_;
    VideoTrackerOptions options_;
    PoseRoi roi_{};
    PoseRoi lost_{};   ///< onde a pessoa sumiu (o detector prefere a mesma ao voltar)
    u32 lostFrames_ = 0;
    OneEuro1 auxFilter_[4];
    bool havePrev_ = false;
    PoseOutput prev_{};
    u32 lastWidth_ = 0, lastHeight_ = 0;
    u32 armRun_ = 0, legRun_ = 0;
    u32 sinceCheck_ = 0;
    u32 detections_ = 0;
    u32 swaps_ = 0;
};

/// Quadro de MUNDO que acompanha o quadro de imagem nas pontes do PC
/// (rig::kMocapWorldFlag): tempo igual, 33 × (x, y, z em metros, visibilidade).
void pack_world_frame(f64 timeSeconds, const PoseOutput& pose, f32* out) noexcept;

} // namespace aurea::mocap

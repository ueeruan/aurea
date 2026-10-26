// =============================================================================
//  Aurea / hyper / HyperNeural.hpp
//
//  A parte PÚBLICA das redes do AureaHyper (src/hyper/neural/**): o que as
//  telas e o medidor do motor chamam além das etapas registradas em
//  `register_neural_stages` (HyperStage.hpp).
//
//    * Detector de rostos da plataforma (ML Kit no Android, Vision no iOS):
//      a tela registra uma função; a etapa Face chama na thread do grafo.
//      Sem detector (host, testes) = nenhum rosto, a etapa só copia.
//    * Pasta de modelos sob demanda (GFPGAN, grande demais para o APK).
//    * Medidor: tempo de um tile Lite no melhor backend + conferência do
//      resultado contra uma referência guardada (driver Vulkan errado → CPU).
//    * Interpolação de quadros direta (A, B, t) para quem tiver os quadros
//      de origem (ver docs/diagnostico/hyper-neural-requests.md).
//
//  Modelos e licenças: engine/assets/hyper/<modelo>/manifest.json e LICENSE.
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/hyper/HyperStage.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace aurea::hyper {

// --- Rostos ------------------------------------------------------------------

/// Um rosto achado pelo detector da plataforma, em pixels do quadro passado.
struct FaceBox {
    f32 x = 0, y = 0, width = 0, height = 0;
    /// 5 pontos (x, y): olho esquerdo, olho direito, ponta do nariz, canto
    /// esquerdo e canto direito da boca — esquerdo/direito NA IMAGEM.
    f32 landmarks[10]{};
    bool hasLandmarks = false;
    f32 yawDegrees = 0.0f;       ///< 0 = de frente; |yaw| grande = perfil (pulado)
    f32 confidence = 1.0f;       ///< 0..1
    u32 trackId = 0;             ///< id estável entre quadros, se o detector tiver (0 = sem)
};

/// Chamado na thread do grafo com um quadro R'G'B' 8 bits. Escreve até
/// `maxBoxes` caixas e devolve quantas. Precisa ser thread-safe com a tela.
using FaceDetectorFn = u32 (*)(void* context, const u8* rgb, u32 width, u32 height, u32 stride,
                               FaceBox* boxes, u32 maxBoxes) noexcept;

/// Registra (ou remove, com nulo) o detector do processo.
void set_face_detector(FaceDetectorFn fn, void* context) noexcept;
[[nodiscard]] bool face_detector_available() noexcept;

// --- Modelos sob demanda -----------------------------------------------------

/// Pasta onde a tela grava os modelos baixados (ex.: <files>/hyper-models).
/// Cada modelo em <pasta>/<id>/ com os arquivos do manifesto.
void set_model_directory(const std::string& path);
[[nodiscard]] std::string model_directory();

/// Modelo sob demanda presente e com o SHA-256 do manifesto (conferido uma vez
/// por processo). Ids: "gfpgan-v1.4".
[[nodiscard]] bool ondemand_model_ready(const char* id) noexcept;

// --- Medidor -----------------------------------------------------------------

struct NeuralMeter {
    bool cpuOk = false;            ///< SPAN ×2 deu a referência na CPU (fp32 e fp16 onde a CPU tem)
    bool cpuInt8Ok = false;        ///< animevideov3 int8 misto ≥ 30 dB do fp32 nesta CPU
    bool vulkanOk = false;         ///< idem no Vulkan (fp32 na conta)
    bool vulkanFp16Ok = false;     ///< idem no Vulkan com conta fp16
    f32 cpuMsPerTile = 0.0f;       ///< um tile Lite (tile + 2·margem) na CPU, modo rápido
    f32 vulkanMsPerTile = 0.0f;
    f32 bestMsPerTile = 0.0f;      ///< o que vai para DeviceProfile::liteMsPerTile
    bool bestIsVulkan = false;
    u32 tile = 0, margin = 0;
    std::string note;              ///< o que foi recusado e por quê
};

/// Roda a referência e cronometra um tile Lite. ~0,2–3 s. `profile` dá tile,
/// margem, abi64 e se o Vulkan foi detectado. Não aloca além de um tile.
[[nodiscard]] NeuralMeter measure_neural(const DeviceProfile& profile,
                                         const std::atomic<bool>* cancel = nullptr) noexcept;

/// Aplica o resultado no perfil (vulkanFp16, liteMsPerTile, backendNote).
void apply_neural_meter(const NeuralMeter& meter, DeviceProfile& profile) noexcept;

// --- Interpolação direta -----------------------------------------------------

/// RIFE v4.6 com o fluxo calculado em resolução reduzida e o quadro novo
/// montado pelo warp dos quadros em resolução cheia. Uma instância por thread.
class MotionInterpolator {
public:
    MotionInterpolator() noexcept;
    ~MotionInterpolator();
    MotionInterpolator(const MotionInterpolator&) = delete;
    MotionInterpolator& operator=(const MotionInterpolator&) = delete;
    /// `path` Lite (fluxo a 1/4) ou Max (1/2; inteiro até 720p).
    [[nodiscard]] Status prepare(const DeviceProfile& profile, Path path, u32 width, u32 height) noexcept;
    /// Quadro em `t` (0..1) entre `a` e `b`. Quadros iguais = cópia (sem rede);
    /// corte de cena = o mais próximo.
    [[nodiscard]] Status interpolate(const FrameView& a, const FrameView& b, f32 t, FrameBuffer& out,
                                     const StageContext& ctx) noexcept;
    /// O mesmo em RGB8 (w×h, stride w·3), para testes e ferramentas.
    [[nodiscard]] Status interpolate_rgb(const u8* a, const u8* b, f32 t, u8* out, const StageContext& ctx) noexcept;
    [[nodiscard]] u64 peak_bytes() const noexcept;
    /// Última decisão: 0 rede, 1 cópia (iguais), 2 corte de cena.
    [[nodiscard]] u32 last_decision() const noexcept;
    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

} // namespace aurea::hyper

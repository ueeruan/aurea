// =============================================================================
//  Aurea / hyper / Hyper.hpp
//
//  AureaHyper (0.3.2): a suíte de restauração do Aurea — upscale, limpeza
//  (ruído, blocos, entrelaçado), rostos, quadros a mais (60 fps, câmera lenta)
//  e estabilização. Substitui o "Upscale com IA" da Home; o upscaler ncnn da
//  0.1.4 (ai/Upscaler.cpp, Real-ESRGAN animevideov3) é o primeiro módulo dele.
//
//  CONTRATO COMUM entre o motor (src/hyper/**, src/engine/EngineHyper.cpp), as
//  redes (src/hyper/neural/**) e as telas (Android/iOS). Mudar algo aqui exige
//  mudar os três lados no mesmo trabalho.
//
//  Cada função tem três CAMINHOS e o aparelho escolhe o padrão:
//    Turbo — shaders e algoritmos clássicos na GPU, qualquer aparelho (32 bits).
//    Lite  — redes pequenas (int8/fp16, tiles de 64–96; 128 em 64 bits).
//    Max   — a rede completa (aparelho bom ou "deixar processando").
//  A prévia na timeline usa sempre Turbo; Lite e Max só no export/pré-exportar.
//
//  Ordem das etapas (fixa, é o que barateia o resto):
//    decodificar → Clean (denoise, deblock, deinterlace; na resolução original)
//    → Stable → Upscale → Face → Motion (interpola DEPOIS do upscale: o upscale
//    só processa os quadros originais) → codificar.
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"

#include <array>
#include <string>

namespace aurea::hyper {

/// Caminho de processamento.
enum class Path : u8 { Auto = 0, Turbo = 1, Lite = 2, Max = 3 };
inline constexpr u32 kPathCount = 4;   ///< índices de Estimate::seconds (Auto = o escolhido)

/// Tipo de conteúdo do upscale (escolhe a rede/o shader).
enum class UpscaleMode : u8 { General = 0, Anime = 1 };

/// A receita: o que fazer com o vídeo. Zeros/false = etapa desligada.
/// Persistida no projeto como parâmetros dos efeitos Hyper e enviada ao job
/// da Home; valores fora da faixa são saneados por `sanitize`.
struct Recipe {
    // Upscale -----------------------------------------------------------------
    u32 upscale = 1;                      ///< 1 (desligado), 2 ou 4
    UpscaleMode upscaleMode = UpscaleMode::General;
    // Clean ---------------------------------------------------------------------
    f32 denoise = 0.0f;                   ///< 0..1 (força; mistura x4v3 ↔ wdn-x4v3 no Lite/Max)
    bool deblock = false;                 ///< artefatos de compressão (blocos)
    u8 deinterlace = 0;                   ///< 0 desligado, 1 automático (só se entrelaçado), 2 sempre
    // Face ----------------------------------------------------------------------
    bool face = false;                    ///< desligado por padrão (risco de "trocar" o rosto)
    f32 faceStrength = 0.5f;              ///< 0..1
    // Motion --------------------------------------------------------------------
    f64 targetFps = 0.0;                  ///< 0 = manter; ex. 60 (quadros novos por interpolação)
    u32 slowMotion = 1;                   ///< 1, 2, 4 ou 8 (duração ×N)
    // Stable --------------------------------------------------------------------
    bool stabilize = false;
    f32 stabilizeStrength = 0.5f;         ///< 0..1 (suavização da trajetória)
    bool stabilizeToKeyframes = false;    ///< grava posição/rotação na camada em vez de recortar
    // Execução ------------------------------------------------------------------
    Path path = Path::Auto;
};

/// Faixas válidas (o motor nunca confia no que veio da tela).
[[nodiscard]] Recipe sanitize(const Recipe& r) noexcept;
/// Nenhuma etapa ligada.
[[nodiscard]] bool is_identity(const Recipe& r) noexcept;

/// O medidor do aparelho (uma vez, ~10 s, na primeira abertura; guardado em
/// disco e refeito quando o app/driver muda). A pessoa pode trocar o caminho.
struct DeviceProfile {
    bool measured = false;
    bool abi64 = false;                   ///< arm64/x86_64 (32 bits = armeabi-v7a)
    u64 freeRamBytes = 0;
    bool vulkan = false;                  ///< Vulkan presente
    bool vulkanFp16 = false;              ///< fp16 suportado E o tile de referência saiu certo
    bool gles3 = false;
    bool npu = false;                     ///< NNAPI (Android) / Core ML (iOS) disponível
    f32 turboMsPerFrame = 0.0f;           ///< FSR 1 480p→1080p, um quadro
    f32 liteMsPerTile = 0.0f;             ///< um tile da rede Lite (SPAN) no melhor backend
    f32 thermalAfter = 0.0f;              ///< nível térmico depois do teste (0..1)
    Path defaultPath = Path::Turbo;       ///< o que o app sugere
    u32 tile = 128;                       ///< tile das redes neste aparelho (64–128)
    u32 tileMargin = 12;                  ///< borda do tile (8–12 px)
    std::string backendNote;              ///< diagnóstico legível (o que foi recusado e por quê)
};

/// Estimativa em segundos para o vídeo inteiro, por caminho (índice = Path).
/// < 0 = caminho indisponível neste aparelho.
struct Estimate {
    std::array<f64, kPathCount> seconds{-1.0, -1.0, -1.0, -1.0};
    u64 peakBytes = 0;                    ///< memória máxima prevista do caminho escolhido
    u32 outputWidth = 0, outputHeight = 0;///< já limitado pelo encoder do aparelho
    f64 outputFps = 0.0;
};

/// Passos visíveis do job (a lista "honesta" da tela 4).
enum class Step : u8 { Prepare = 0, Clean, Stable, Upscale, Face, Motion, Encode, Finish, Count };
inline constexpr u32 kStepCount = static_cast<u32>(Step::Count);

/// Estado do job da Home (`Engine::hyper_job_steps`). Uma cópia: ler não
/// trava nada. Os quadros/ETA do encode continuam em `Engine::export_progress`.
struct JobSteps {
    bool active = false;                  ///< houve um start_hyper_job nesta execução do motor
    bool running = false;
    bool finished = false;
    Errc result = Errc::Ok;               ///< Cancelled depois de cancel_export
    std::array<bool, kStepCount> present{};   ///< o passo existe neste job (índice = Step)
    std::array<bool, kStepCount> done{};
    /// Caminho que cada etapa usa de fato (Clean..Motion; Turbo/Lite/Max). O
    /// pedido Lite/Max cai para Turbo quando a rede não existe no aparelho.
    std::array<Path, kStepCount> paths{};
    Step current = Step::Prepare;
    f32 stepProgress = 0.0f;              ///< 0..1 do passo atual
    f32 progress = 0.0f;                  ///< 0..1 do job inteiro
    u32 pass = 0, passCount = 0;          ///< passagens pelo export (1 só Turbo; 2–3 com redes)
    bool audioMuted = false;              ///< câmera lenta: o som sai mudo (não é esticado)
    u32 outputWidth = 0, outputHeight = 0;
    f64 outputFps = 0.0;
    u32 outputFrames = 0;
    char message[128]{};                  ///< pt-BR, para o log/diagnóstico
};

/// Chaves dos efeitos de camada (editor). Registrados no catálogo de efeitos.
namespace effect_keys {
inline constexpr const char* kHyperUpscale = "aurea.hyper.upscale";
inline constexpr const char* kHyperMotion  = "aurea.hyper.motion";
inline constexpr const char* kHyperClean   = "aurea.hyper.clean";
} // namespace effect_keys

/// Identificador do produto pago (compra única). O portão vive nas telas; o
/// motor não decide quem pagou.
inline constexpr const char* kProductId = "aureahyper_lifetime";

} // namespace aurea::hyper

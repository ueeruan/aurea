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
#include <string_view>
#include <vector>

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

/// Identificador do produto pago LEGADO (compra única, concedido por conta pelo
/// dono no servidor). Continua liberando TUDO o que é pago. O portão vive nas
/// telas; o motor não decide quem pagou.
inline constexpr const char* kProductId = "aureahyper_lifetime";

/// 16/11/2026 (decisão do dono): o conteúdo pago vira ASSINATURA MENSAL.
///   · AureaHyper (kHyperSubscriptionId, R$ 59,50/mês) libera TUDO o que é pago
///     (cada efeito pago, o Texto 3D e as 3 ferramentas Hyper);
///   · cada efeito pago e o Texto 3D são uma assinatura própria (R$ 5,00/mês).
/// Os preços vivem nas lojas; os ids abaixo são os mesmos na Play e na App Store
/// (docs/release/assinaturas.md). As telas perguntam ao motor qual produto
/// libera um item e se o conjunto de produtos da conta dá acesso a ele — a
/// tabela não se repete no Kotlin/Swift.
/// 1.0.2.1 (pedido do dono, 19/11/2026): só existe o AureaHyper ANUAL (R$ 100/ano),
/// que libera tudo; acabaram a mensalidade e as assinaturas avulsas. É o único
/// produto que as telas oferecem. O AureaHyper mensal e os avulsos de antes
/// continuam reconhecidos (quem já assinou não perde nada), mas não se vendem.
inline constexpr const char* kHyperSubscriptionId = "aurea_hyper_anual";
/// O AureaHyper mensal de 16/11 a 19/11/2026 (legado: libera tudo, não se vende).
inline constexpr const char* kHyperMonthlyLegacyId = "aurea_hyper_mensal";
inline constexpr const char* kText3DProductId = "aurea_texto3d";

/// Classificação comum do catálogo. As plataformas continuam verificando a
/// compra da conta antes de adicionar um efeito desse produto.
[[nodiscard]] bool effect_requires_access(u32 effectTypeId) noexcept;

/// 1.0.2 (pedido do dono, 18/11/2026): uma OPÇÃO paga dentro de um efeito
/// grátis. Hoje só o Flow Retime › Qualidade dos vetores › "Neural (IA)" (a
/// mesma rede RIFE do Hyper Motion): só no AureaHyper, sem assinatura avulsa.
/// ESCOLHER a opção pede o acesso nas telas; um projeto que já a usa abre,
/// mostra e exporta igual (mesma regra do efeito pago já aplicado).
[[nodiscard]] bool effect_option_requires_access(u32 effectTypeId, u32 paramIndex, f32 value) noexcept;
/// O índice do parâmetro "Qualidade dos vetores" do Flow Retime e o valor Neural.
inline constexpr u32 kFlowRetimeVectorsParam = 2;
inline constexpr u32 kFlowRetimeNeuralValue = 3;

/// 0.5.4 (pedido do dono, 15/11/2026): o TEXTO 3D é do mesmo produto pago.
/// CRIAR texto 3D (+ › Texto 3D, presets de texto 3D, o dock da cena) pede o
/// acesso nas telas, como adicionar um efeito pago. Um texto 3D que JÁ está no
/// projeto se comporta como um efeito pago já aplicado sem o acesso: abre,
/// edita, desenha na prévia e exporta igual (sem marca d'água), só leva o
/// cadeado. O motor não decide quem pagou (ver kProductId).
[[nodiscard]] bool text3d_requires_access() noexcept;
/// O asset de modelo com esta origem é um texto 3D pago (receita
/// "aurea-text3d:")? Formas 3D e imagem para 3D continuam livres.
[[nodiscard]] bool model_source_requires_access(const std::string& sourcePath) noexcept;

/// A assinatura AVULSA que libera este efeito pago (ex. "aurea_fx_twitch"), ou
/// nullptr: efeito grátis, ou só no AureaHyper (as 3 ferramentas Hyper). As
/// versões legadas (kTwitch, kSignal, kGlitchify, kRasterExtrude) apontam para
/// a mesma assinatura da referência AE.
[[nodiscard]] const char* effect_product_id(u32 effectTypeId) noexcept;
/// A assinatura avulsa do Texto 3D (kText3DProductId).
[[nodiscard]] const char* text3d_product_id() noexcept;
/// Este produto libera TUDO o que é pago? (AureaHyper mensal e o legado vitalício.)
[[nodiscard]] bool product_unlocks_everything(std::string_view productId) noexcept;
/// Os produtos que o app reconhece: o AureaHyper anual primeiro (o ÚNICO à venda
/// desde 1.0.2.1), depois o mensal legado e cada avulso legado (ordem estável,
/// sem repetição). As telas usam para restaurar; à venda só kHyperSubscriptionId.
[[nodiscard]] std::vector<std::string> subscription_product_ids();

/// A REGRA de acesso (uma só, para as duas plataformas): a conta que tem
/// `owned` pode usar o item pago cuja assinatura avulsa é `itemProduct`
/// (nullptr/"" = item só do AureaHyper) quando tem o AureaHyper mensal, o
/// legado vitalício ou a assinatura do próprio item.
[[nodiscard]] bool has_access(const std::vector<std::string>& owned, const char* itemProduct) noexcept;
/// A regra aplicada a um efeito: grátis passa sempre; pago segue `has_access`.
[[nodiscard]] bool has_effect_access(const std::vector<std::string>& owned, u32 effectTypeId) noexcept;
/// A regra aplicada a CRIAR texto 3D.
[[nodiscard]] bool has_text3d_access(const std::vector<std::string>& owned) noexcept;

} // namespace aurea::hyper

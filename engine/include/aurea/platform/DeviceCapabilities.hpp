// =============================================================================
//  Aurea / platform / DeviceCapabilities.hpp
//
//  O que ESTE aparelho consegue fazer, medido e não suposto.
//
//  Toda decisão adaptativa do Aurea sai daqui: quantos workers o pool tem, qual
//  resolução o preview usa, quais efeitos entram na versão de preview, se o
//  decode é feito direto para GPU ou passa por memória, qual LOD carregar.
//
//  A regra é: nunca perguntar "é um celular?" e assumir o pior. Perguntar
//  "quantos decoders HEVC 4K este aparelho tem?" e decidir por isso. Um
//  Snapdragon 8 Gen 3 e um aparelho de entrada recebem o mesmo código com
//  planos diferentes.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/platform/DevicePolicy.hpp"

#include <string>

namespace aurea {

struct CodecCapability {
    bool supported        = false;
    bool hardwareAccelerated = false;
    u32  maxWidth         = 0;
    u32  maxHeight        = 0;
    u32  maxFps           = 0;
    u32  maxBitrate       = 0;
    u8   maxBitDepth      = 8;
    bool supportsHdr      = false;
    /// Quantas instâncias simultâneas o decoder suporta. É o que limita
    /// quantos vídeos podem tocar ao mesmo tempo sem reabrir a sessão a cada
    /// frame — e reabrir sessão de decoder de hardware custa ~30 ms.
    u32  concurrentInstances = 0;
    bool lowLatency       = false;
    bool fecSupported     = false;
    std::string name;
};

struct GpuCapabilities {
    std::string deviceName;
    std::string driverVersion;
    u32  vendorId = 0;
    u32  deviceId = 0;

    bool vulkan       = false;
    bool metal        = false;
    bool openGLES     = false;

    u32  apiVersionMajor = 0;
    u32  apiVersionMinor = 0;

    /// Limites que decidem tamanho de render target e orçamento de memória.
    u32  maxTextureSize     = 2048;
    u32  maxTextureLayers   = 256;
    u32  maxComputeWorkgroupSize = 128;
    u32  maxStorageBufferRange   = 128u * 1024 * 1024;
    u32  maxDrawIndirectCount    = 0;

    bool supportsCompute          = false;
    bool supportsFloat16          = false;
    bool supportsFloat16Storage   = false;
    bool supportsDepthTexture     = false;
    bool supportsAnisotropicFiltering = false;
    bool supportsAstcCompression  = false;
    bool supportsEtc2Compression   = false;
    bool supportsTimestampQueries = false;

    /// Memória de GPU, quando a plataforma informa. Zero = desconhecida, e
    /// nesse caso o orçamento vem de uma fração da RAM do sistema.
    u64  totalVideoMemoryBytes = 0;
    u64  totalSystemMemoryBytes = 0;

    /// Teto de trabalho de fragmento por frame. Calculado a partir de
    /// `deviceName` mais a medição de banda na primeira execução: é a base do
    /// preview adaptativo decidir 1/2 ou 1/4.
    u64  estimatedFillRatePerSec = 0;
};

/// Núcleos separados por tipo. Um pool que usa os 8 núcleos de um big.LITTLE
/// para render perde para um que usa só os 3 grandes — os pequenos atrasam a
/// barreira e o frame inteiro espera por eles.
struct CpuCapabilities {
    u32 totalCores      = 1;
    u32 performanceCores = 1;
    u32 efficiencyCores  = 1;
    u32 maxFrequencyKhz  = 0;
    u64 totalMemoryBytes = 0;
    /// Memória que o app pode usar antes de o sistema começar a matar.
    u64 availableMemoryBytes = 0;
    /// Chamada de sistema para memória disponível + limpeza de página:
    /// usado pelo MemoryManager para apertar o orçamento sob pressão.
    bool lowMemoryKillerAvailable = false;
};

struct ThermalState {
    enum class Level : u8 { Nominal = 0, Fair, Serious, Critical, Emergency, Unknown };
    Level level = Level::Unknown;
    bool  throttling = false;
    bool  batteryLow = false;
    bool  charging   = false;
    f32   batteryLevel = 1.0f;

    /// Vira true quando o aparelho já reduziu clock E nós já vimos quedas de
    /// FPS sustentadas. É a ordem de degradação do preview.
    [[nodiscard]] bool should_degrade() const noexcept {
        return throttling || level == Level::Serious
            || level == Level::Critical || level == Level::Emergency;
    }
    /// No nível crítico o preview corta resolução E efeitos pesados.
    [[nodiscard]] bool severe() const noexcept {
        return level == Level::Critical || level == Level::Emergency;
    }
    /// A faixa da política (DevicePolicy.hpp). `throttling` sobe uma faixa
    /// até HOT: o sistema já baixou clock, e isso vale mais que o rótulo.
    [[nodiscard]] ThermalTier tier() const noexcept {
        switch (level) {
            case Level::Nominal:   return throttling ? ThermalTier::Warm : ThermalTier::Normal;
            case Level::Fair:      return throttling ? ThermalTier::Hot : ThermalTier::Warm;
            case Level::Serious:   return ThermalTier::Hot;
            case Level::Critical:
            case Level::Emergency: return ThermalTier::Critical;
            default:               return throttling ? ThermalTier::Warm : ThermalTier::Normal;
        }
    }
};

/// `PowerManager.THERMAL_STATUS_*` (0 NONE … 6 SHUTDOWN) → estado do motor.
/// Uma tabela só, testada no host; o JNI só repassa o número.
[[nodiscard]] constexpr ThermalState thermal_state_from_android(i32 status) noexcept {
    ThermalState t;
    t.level = status <= 0 ? ThermalState::Level::Nominal
            : status <= 2 ? ThermalState::Level::Fair
            : status == 3 ? ThermalState::Level::Serious
            : status == 4 ? ThermalState::Level::Critical
                          : ThermalState::Level::Emergency;
    t.throttling = status >= 2;
    return t;
}

/// Por que o teto de export é o que é — a UI diz a frase certa (§109).
enum class ExportLimit : u8 {
    Unknown = 0,   ///< sem tabela de codecs (host, ou sondagem falhou): teto padrão
    None,          ///< o codificador dá conta de 4K
    Encoder,       ///< o codificador de vídeo do aparelho não passa do teto
    Memory,        ///< a RAM não segura os quadros de 4K do export
    NoEncoder,     ///< a tabela veio sem codificador H.264: teto conservador
};

/// Opção de escala concreta, já resolvida em pixels para a composição atual.
/// A UI monta o seletor AUTO / FULL / 1/2 / 1/4 / 1/8 a partir da lista que o
/// controlador adaptativo produz — com a resolução já calculada, para que o
/// usuário veja "960x540" e não "1/2".
struct PreviewScaleOption {
    PreviewScale scale = PreviewScale::Auto;
    u32 numerator   = 1;
    u32 denominator = 1;
    u32 width       = 0;
    u32 height      = 0;
};

/// Informação que SÓ a plataforma sabe e que a Kotlin/Swift coleta.
///
/// Por que passar por aqui em vez de o motor chamar JNI/Objective-C direto: o
/// núcleo C++ fica sem dependência de <jni.h> nem de <MediaCodec.h>. Isso
/// significa que ele compila no host e roda nos testes de integração — a
/// lógica de decisão (quanto de preview, quantos workers, qual codec) é
/// testável sem aparelho na mão.
struct PlatformInfo {
    u32  totalCores = 0;
    u32  performanceCores = 0;
    u32  efficiencyCores = 0;
    u32  maxFrequencyKhz = 0;
    u64  totalMemoryBytes = 0;
    u64  availableMemoryBytes = 0;
    u32  displayRefreshRate = 60;
    bool displaySupportsHdr = false;

    GpuCapabilities gpu{};

    CodecCapability decoders[16]{};
    u32  decoderCount = 0;
    CodecCapability encoders[8]{};
    u32  encoderCount = 0;

    /// Mapa de codecTag ('avc1', 'hvc1', 'av01', 'vp09') para o índice em
    /// `decoders`. 0xFFFFFFFF = ausente.
    ///
    /// O padrão TEM de ser "ausente". Era `{}` (zeros): todo tag que a tabela
    /// não trazia apontava para o codec 0 — num aparelho sem HEVC, o HEVC
    /// "existia" e era o decoder H.264. A classe do aparelho e o aviso
    /// "HEVC indisponível" (§109) dependem disto.
    u32 decoderIndexForTag[8]{kInvalidIndex, kInvalidIndex, kInvalidIndex, kInvalidIndex,
                              kInvalidIndex, kInvalidIndex, kInvalidIndex, kInvalidIndex};
    u32 encoderIndexForTag[8]{kInvalidIndex, kInvalidIndex, kInvalidIndex, kInvalidIndex,
                              kInvalidIndex, kInvalidIndex, kInvalidIndex, kInvalidIndex};

    /// Liga um tag ('avc1', 'hvc1', 'av01', 'vp09') ao índice em `decoders`.
    ///
    /// O slot sai DO TAG, não do chamador: quem preenche a tabela pensa em
    /// "achei um HEVC", não em "slot 1". Antes o slot vinha por parâmetro e o
    /// tag era descartado — trocar a ordem de dois codecs ligava o decoder
    /// errado em silêncio.
    void set_decoder_tag(u32 codecTag, u32 index) noexcept {
        if (const u32 slot = tag_slot_of(codecTag); slot < 8) decoderIndexForTag[slot] = index;
    }

    /// O mesmo para encoder.
    void set_encoder_tag(u32 codecTag, u32 index) noexcept {
        if (const u32 slot = tag_slot_of(codecTag); slot < 8) encoderIndexForTag[slot] = index;
    }

    /// Slot de um tag de codec; 8 = desconhecido.
    [[nodiscard]] static u32 tag_slot_of(u32 codecTag) noexcept {
        switch (codecTag) {
            case 0x61766331u: return 0;   // 'avc1'
            case 0x68766331u: return 1;   // 'hvc1'
            case 0x61763031u: return 2;   // 'av01'
            case 0x76703039u: return 3;   // 'vp09'
            default: return 8;
        }
    }
};

class DeviceCapabilities {
public:
    DeviceCapabilities() = default;

    /// Coleta tudo. Chamado uma vez na inicialização, fora da thread da UI —
    /// a sondagem de codecs e a criação de um device Vulkan/Metal descartável
    /// custam dezenas de ms.
    ///
    /// No Android e no iOS a parte de codec e de GPU vem de
    /// `apply_platform_info`, preenchida pela bridge. No host, a detecção usa
    /// o que o sistema operacional informa — e o que não for detectável fica
    /// no padrão CONSERVADOR, nunca num valor otimista inventado.
    void detect() noexcept;

    /// Aplica o que a camada de plataforma mediu. Chamar ANTES de `detect()`
    /// ou a informação será sobrescrita pela detecção genérica.
    void apply_platform_info(const PlatformInfo& info) noexcept;

    /// Aplica a GPU REAL, medida pelo backend depois que ele sobe.
    ///
    /// Por que isto existe separado de `apply_platform_info`: a GPU é a única
    /// coisa que a camada de plataforma não consegue medir de fora — é preciso
    /// ter criado o dispositivo Vulkan/Metal para perguntar os limites. Então
    /// ela chega DEPOIS de `detect()`, e sozinha, sem tocar em codec nenhum.
    ///
    /// Sem esta chamada o motor decide com o padrão conservador (max_textura
    /// 2048, GPU desconhecida) em qualquer aparelho — inclusive num que suporta
    /// 16384. Recomputa o orçamento. Devolve true se algo mudou.
    bool apply_gpu(const GpuCapabilities& gpu) noexcept;

    /// Sondagem só do que é barato. Usada em segundo plano para atualizar
    /// memória disponível e estado térmico sem parar o editor.
    void refresh_dynamic() noexcept;

    /// Estado térmico e de bateria, informado pela plataforma. Android:
    /// PowerManager. iOS: ProcessInfo.thermalState.
    void set_thermal_state(const ThermalState& state) noexcept { thermal_ = state; }

    // --- Classe e política (Fase 8H) --------------------------------------------

    /// Classe do aparelho (LOW/MID/HIGH/ULTRA) pelo que foi medido. Calculada
    /// em `detect()` e de novo quando a GPU real chega (`apply_gpu`) — não em
    /// `refresh_dynamic`: a classe não pode oscilar no meio da sessão porque a
    /// RAM livre mudou.
    [[nodiscard]] const DeviceClass& device_class() const noexcept { return class_; }
    [[nodiscard]] DeviceTier tier() const noexcept { return class_.tier; }
    [[nodiscard]] ThermalTier thermal_tier() const noexcept { return thermal_.tier(); }
    /// O que o motor faz AGORA: classe + temperatura.
    [[nodiscard]] DevicePolicy policy() const noexcept { return device_policy(class_.tier, thermal_.tier()); }
    /// A entrada da classificação, montada do que foi medido.
    [[nodiscard]] DeviceClassInput class_input() const noexcept;

    /// A plataforma mandou a tabela de codecs (MediaCodecList)?
    [[nodiscard]] bool codecs_known() const noexcept { return platform_.decoderCount + platform_.encoderCount > 0; }
    [[nodiscard]] ExportLimit export_limit() const noexcept { return exportLimit_; }

    // --- Consultas ------------------------------------------------------------

    [[nodiscard]] const CpuCapabilities&  cpu()  const noexcept { return cpu_; }
    [[nodiscard]] const GpuCapabilities&  gpu()  const noexcept { return gpu_; }
    [[nodiscard]] const ThermalState&     thermal() const noexcept { return thermal_; }

    [[nodiscard]] const CodecCapability& decoder_h264() const noexcept { return decodeH264_; }
    [[nodiscard]] const CodecCapability& decoder_hevc() const noexcept { return decodeHevc_; }
    [[nodiscard]] const CodecCapability& decoder_av1()  const noexcept { return decodeAv1_; }
    [[nodiscard]] const CodecCapability& decoder_vp9()  const noexcept { return decodeVp9_; }

    [[nodiscard]] const CodecCapability& encoder_h264() const noexcept { return encodeH264_; }
    [[nodiscard]] const CodecCapability& encoder_hevc() const noexcept { return encodeHevc_; }
    [[nodiscard]] const CodecCapability& encoder_av1()  const noexcept { return encodeAv1_; }

    /// Decoder adequado para um codec, com fallback documentado. Devolve uma
    /// capability com `supported == false` quando não há — e nesse caso o
    /// chamador precisa usar o caminho de software (e avisar a UI, porque a
    /// reprodução vai ser mais lenta que o tempo real).
    [[nodiscard]] const CodecCapability& best_decoder_for(u32 codecTag) const noexcept;

    /// Quantos workers o JobSystem deve subir. Nunca usa todos os núcleos:
    /// dois ficam livres para o sistema e para a thread de render, senão o
    /// scheduler do SO tira a thread de render da CPU no meio do frame.
    [[nodiscard]] u32 recommended_worker_count() const noexcept;

    /// Resolução recomendada para começar. É um PONTO DE PARTIDA — o preview
    /// adaptativo ajusta em tempo real a partir da medição de frame time.
    [[nodiscard]] PreviewScale recommended_initial_scale(u32 compWidth, u32 compHeight) const noexcept;

    /// Teto de resolução do preview. Acima disto o preview sempre reduz.
    [[nodiscard]] u32 max_preview_width()  const noexcept { return maxPreviewWidth_; }
    [[nodiscard]] u32 max_preview_height() const noexcept { return maxPreviewHeight_; }

    /// Teto de resolução de export: LADO MAIOR × LADO MENOR (um 1920×1080
    /// também exporta 1080×1920). Com a tabela de codecs, é o do CODIFICADOR
    /// H.264 — é ele que escreve o arquivo; o decoder só limita a fonte. O
    /// motivo fica em `export_limit()`, para a UI não esconder a opção.
    [[nodiscard]] u32 max_export_width()  const noexcept { return maxExportWidth_; }
    [[nodiscard]] u32 max_export_height() const noexcept { return maxExportHeight_; }

    /// Tamanho máximo de textura carregada direto. Textura maior é reduzida na
    /// importação — nunca se carrega 8K para desenhar 200 px.
    [[nodiscard]] u32 max_texture_dimension() const noexcept { return gpu_.maxTextureSize; }

    /// Formato de compressão de textura preferido (ASTC onde há, ETC2 no resto).
    [[nodiscard]] u32 preferred_texture_compression() const noexcept {
        if (gpu_.supportsAstcCompression) return 1;   // ASTC
        if (gpu_.supportsEtc2Compression) return 2;   // ETC2
        return 0;                                     // RGBA sem compressão
    }

    /// Quantas threads de decode paralelo. Limitado por `concurrentInstances`
    /// dos decoders de hardware: mandar 8 decodes para um aparelho que só tem
    /// 2 instâncias faz os 6 restantes esperarem a sessão liberar.
    [[nodiscard]] u32 decode_parallelism() const noexcept;

    /// Bytes que o MemoryManager pode orçar no total.
    [[nodiscard]] u64 memory_budget_bytes() const noexcept;

    /// Soma de tudo que foi detectado, para o painel de telemetria.
    [[nodiscard]] std::string summary() const;

    /// Marca que a detecção rodou. Sem isto, as consultas devolvem os padrões
    /// conservadores — nunca um valor otimista inventado.
    [[nodiscard]] bool detected() const noexcept { return detected_; }

private:
    void detect_cpu() noexcept;
    void detect_gpu_host() noexcept;
    void apply_platform_codecs() noexcept;
    void compute_budget() noexcept;

    /// Estado térmico padrão do host — sempre Nominal, porque o host não
    /// estrangula. Nos aparelhos, a plataforma sobrescreve.
    PlatformInfo platform_{};

    CpuCapabilities cpu_{};
    GpuCapabilities gpu_{};
    ThermalState    thermal_{};

    CodecCapability decodeH264_{}, decodeHevc_{}, decodeAv1_{}, decodeVp9_{};
    CodecCapability encodeH264_{}, encodeHevc_{}, encodeAv1_{};

    u32 maxPreviewWidth_  = 1920;
    u32 maxPreviewHeight_ = 1080;
    u32 maxExportWidth_   = 3840;
    u32 maxExportHeight_  = 2160;
    ExportLimit exportLimit_ = ExportLimit::Unknown;

    DeviceClass class_{};

    bool detected_ = false;
};

/// O relatório "Este aparelho", em números, slot a slot — o layout que o
/// `DeviceReport.kt` lê. Mora aqui (e não no JNI) para ser testado no host.
///   0 núcleos · 1 grandes · 2 pequenos · 3 RAM total (MB) · 4 RAM livre (MB)
///   5 orçamento (MB) · 6 maior textura · 7/8 teto do preview (L × A)
///   9/10 teto do export (lado maior × lado menor) · 11 decodes paralelos
///   12 workers · 13 escala inicial de um 1080p (PreviewScale)
///   14 classe · 15 eixo que segurou (DeviceLimit) · 16..19 faixa de memória,
///   GPU, CPU e codecs · 20 bits (ver kReport*) · 21 ExportLimit
///   22/23 teto do codificador HEVC (lado maior × menor; 0 = sem HEVC)
///   24 heavyScale × 100 AGORA · 25 denominador inicial do preview
///   26 lado das prévias de efeito · 27 % do orçamento para decode
///   28 faixa térmica AGORA · 29 sombra (px) · 30 proxy acima de (lado menor)
///   31 frequência máxima da CPU (MHz; 0 = não medida)
inline constexpr u32 kDeviceReportSlots = 32;
inline constexpr i64 kReportCodecsKnown  = 1 << 0;
inline constexpr i64 kReportHwDecodeH264 = 1 << 1;
inline constexpr i64 kReportHwDecodeHevc = 1 << 2;
inline constexpr i64 kReportHwDecode4K   = 1 << 3;
inline constexpr i64 kReportEncodeH264   = 1 << 4;
inline constexpr i64 kReportEncodeHevc   = 1 << 5;
inline constexpr i64 kReportGpuKnown     = 1 << 6;
inline constexpr i64 kReportHwEncodeH264 = 1 << 7;
void write_device_report(const DeviceCapabilities& caps, i64 out[kDeviceReportSlots]) noexcept;

} // namespace aurea

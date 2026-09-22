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
    u32 decoderIndexForTag[8]{};
    u32 encoderIndexForTag[8]{};

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

    /// Teto de resolução de export. Um aparelho que não decodifica 4K não deve
    /// deixar o usuário escolher export 4K sem aviso.
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

    bool detected_ = false;
};

} // namespace aurea

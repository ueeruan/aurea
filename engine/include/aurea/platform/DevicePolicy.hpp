// =============================================================================
//  Aurea / platform / DevicePolicy.hpp
//
//  Fase 8H (§107–112, §35–38): a CLASSE do aparelho e a POLÍTICA que sai dela.
//
//  Duas perguntas separadas, de propósito:
//
//   1. "Que aparelho é este?" — `classify_device`. LOW / MID / HIGH / ULTRA a
//      partir do que foi MEDIDO (RAM, limites da GPU, CPU, codecs), nunca do
//      nome do modelo. Cada eixo dá a sua faixa e a classe é a MENOR delas: um
//      celular com GPU de topo e 3 GB de RAM é um aparelho de 3 GB. O eixo que
//      segurou fica registrado (`DeviceLimit`) para a UI dizer o porquê.
//
//   2. "O que o motor faz com isso agora?" — `device_policy(classe, térmico)`.
//      Valores concretos que o motor usa: escala inicial do preview, fração do
//      custo das operações caras, tamanho de sombra, fatia do cache de decode,
//      tamanho das prévias de efeito, ritmo do trabalho de fundo, fila do
//      export. A temperatura entra aqui, por cima da classe.
//
//  Funções puras: mesma entrada, mesma saída, no host e no aparelho. Os testes
//  (`test_device.cpp`) exercitam perfis sintéticos de aparelho — o de entrada
//  de 3 GB com Mali antigo e sem HEVC não precisa estar na bancada.
//
//  ONDE CADA CAMPO SE LIGA (quem usa hoje e quem liga depois) está anotado em
//  cada campo de `DevicePolicy`. "Hoje" = o motor já aplica; "gancho" = o
//  valor está decidido e testado, e a frente dona do sistema o consome quando
//  expuser o knob (8C preview adaptativo, 8E 3D/partículas/flow, 8F export).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

namespace aurea {

enum class DeviceTier : u8 { Low = 0, Mid = 1, High = 2, Ultra = 3 };

/// Qual eixo segurou a classe. Empate: a ordem abaixo decide (memória
/// primeiro, porque é o que mata o processo; codec por último, porque só
/// limita parte do trabalho).
enum class DeviceLimit : u8 {
    None = 0,         ///< nada segurou (ULTRA)
    Memory,           ///< RAM total do aparelho
    MemoryPressure,   ///< RAM disponível AGORA (outros apps ocupando)
    Gpu,              ///< limites medidos da GPU (API, compute, fp16, textura)
    GpuUnknown,       ///< GPU não medida: não se promove acima de MID no escuro
    Cpu,              ///< frequência máxima / número de núcleos
    Codecs,           ///< decode de hardware (sem H.264 hw, sem HEVC, sem 4K)
};

/// O que a classificação lê. Zero/false = não medido — e o eixo não medido
/// não puxa a classe para baixo nem para cima (exceto a GPU, ver GpuUnknown).
struct DeviceClassInput {
    u64  totalMemoryBytes     = 0;
    u64  availableMemoryBytes = 0;
    u32  totalCores           = 0;
    u32  performanceCores     = 0;
    u32  maxFrequencyKhz      = 0;

    bool gpuKnown             = false;   ///< o backend subiu e respondeu
    u32  gpuApiMajor          = 0;
    u32  gpuApiMinor          = 0;
    u32  gpuMaxTexture        = 0;
    u32  gpuMaxWorkgroup      = 0;       ///< maxComputeWorkGroupInvocations
    bool gpuCompute           = false;
    bool gpuFp16              = false;
    bool gpuFp16Storage       = false;

    bool codecsKnown          = false;   ///< a plataforma mandou a tabela de codecs
    bool hwDecodeH264         = false;
    bool hwDecodeHevc         = false;
    /// Maior decode de HARDWARE, como lado maior × lado menor.
    u32  hwDecodeMaxLong      = 0;
    u32  hwDecodeMaxShort     = 0;
};

struct DeviceClass {
    DeviceTier  tier   = DeviceTier::Mid;
    DeviceTier  memory = DeviceTier::Ultra;
    DeviceTier  gpu    = DeviceTier::Mid;
    DeviceTier  cpu    = DeviceTier::Ultra;
    DeviceTier  codecs = DeviceTier::Ultra;
    DeviceLimit limit  = DeviceLimit::GpuUnknown;
};

/// Classe do aparelho. Sem medição nenhuma (`DeviceClassInput{}`) dá MID
/// limitado por GpuUnknown: nem o plano de topo (que num aparelho fraco
/// derruba o preview), nem o de entrada (que num aparelho bom esconde o que
/// ele faz) — o preview adaptativo corrige a partir do tempo de quadro real.
[[nodiscard]] DeviceClass classify_device(const DeviceClassInput& in) noexcept;

[[nodiscard]] const char* device_tier_name(DeviceTier t) noexcept;

// -----------------------------------------------------------------------------
// Temperatura (§35–36)
// -----------------------------------------------------------------------------

/// As quatro faixas da política. O nível fino do sistema (7 no Android, 4 no
/// iOS) é traduzido para estas — a política não depende da plataforma.
enum class ThermalTier : u8 {
    Normal = 0,   ///< nada a fazer
    Warm,         ///< menos trabalho de fundo (miniaturas, análise)
    Hot,          ///< preview mais barato: partículas, sombras, flow, motion blur
    Critical,     ///< preview no mínimo, fundo quase parado; editor responde
};

[[nodiscard]] const char* thermal_tier_name(ThermalTier t) noexcept;

/// `PowerManager.THERMAL_STATUS_*` → faixa:
///   0 NONE → NORMAL · 1 LIGHT → WARM · 2 MODERATE, 3 SEVERE → HOT ·
///   4 CRITICAL, 5 EMERGENCY, 6 SHUTDOWN → CRITICAL.
/// MODERATE já é HOT: o sistema está baixando clock, e reduzir o preview
/// depois do engasgo é tarde.
[[nodiscard]] constexpr ThermalTier thermal_tier_from_android(i32 status) noexcept {
    return status <= 0 ? ThermalTier::Normal
         : status == 1 ? ThermalTier::Warm
         : status <= 3 ? ThermalTier::Hot
                       : ThermalTier::Critical;
}

// -----------------------------------------------------------------------------
// Política
// -----------------------------------------------------------------------------
struct DevicePolicy {
    DeviceTier  tier    = DeviceTier::Mid;
    ThermalTier thermal = ThermalTier::Normal;

    // --- Preview ----------------------------------------------------------------
    /// Denominador com que o preview AUTO COMEÇA (1 = cheio, 4 = 1/4).
    /// Hoje: `DeviceCapabilities::recommended_initial_scale` → AdaptiveResolutionController::configure.
    u32 previewInitialDenominator = 1;
    /// Denominador MÍNIMO sob calor (HOT 2, CRITICAL 4).
    /// Gancho 8C: o AdaptiveResolutionController hoje só força descer em
    /// `ThermalState::severe()` (= CRITICAL) e só bloqueia subir em HOT; a
    /// escada nova usa este piso no lugar dos dois testes.
    u32 minPreviewDenominator = 1;
    /// Fração do custo das operações caras do PREVIEW (1 = completo).
    /// Hoje: `RenderSettings::heavyScale` (Engine::preview_heavy_scale) →
    /// amostras de motion blur, contagem de partículas, resolução do optical
    /// flow; ≤ 0,25 troca o movimento de pixels pela mistura de quadros.
    f32 heavyScale = 1.0f;
    /// Flow de alta qualidade no preview (= heavyScale > 0,25). Informativo:
    /// é o que a UI diz quando o preview passa a usar a mistura.
    bool previewOpticalFlow = true;

    // --- 3D / partículas (gancho 8E) --------------------------------------------
    /// Lado do mapa de sombra do preview. Hoje o SceneRenderer usa 2048 fixo
    /// (`shadowSize_`); o knob de 8E lê daqui. Export: sempre o da qualidade final.
    u32 shadowMapSize = 2048;
    /// Fração das partículas do preview. Hoje já chega pelo heavyScale; o
    /// knob separado de 8E usa este valor.
    f32 particleScale = 1.0f;

    // --- Mídia ------------------------------------------------------------------
    /// Fonte com lado menor ACIMA disto prefere proxy no preview (0 = nunca).
    /// Gancho 8C/8E: o motor ainda não gera proxy de preview (só o de análise
    /// do rastreio); quem ligar o proxy consulta este limite.
    u32 preferProxyAboveShortSide = 0;
    /// Fatia do orçamento de memória para quadros decodificados (%).
    /// Hoje: Engine::apply_memory_budgets (MemoryClass::DecodedFrames).
    u32 decodedFramesBudgetPercent = 24;
    /// Maior lado das prévias do navegador de efeitos (px).
    /// Hoje: Engine::render_effect_preview reduz o pedido a este teto.
    u32 effectPreviewMaxSide = 320;

    // --- Trabalho de fundo --------------------------------------------------------
    /// Pausa entre duas tarefas de fundo (ms). Hoje: ThumbnailService (miniaturas
    /// da timeline). Gancho 8C: a prioridade LOW/BACKGROUND do JobSystem.
    u32 backgroundPauseMs = 0;

    // --- Export (§37: menos paralelismo, NUNCA menos qualidade) ------------------
    /// Quadros em voo no pipeline decode→render→encode. Gancho 8F: o export de
    /// hoje é serial (profundidade 1 na prática); o pipeline sobreposto usa
    /// este teto. O calor só tira paralelismo.
    u32 exportPipelineDepth = 3;
    /// Invariantes do export, sob qualquer classe e temperatura: resolução e
    /// custo das operações caras cheios. Estão aqui para o teste travar.
    f32 exportRenderScale = 1.0f;
    f32 exportHeavyScale  = 1.0f;
};

/// A política para uma classe de aparelho sob uma faixa térmica.
[[nodiscard]] DevicePolicy device_policy(DeviceTier tier, ThermalTier thermal) noexcept;

} // namespace aurea

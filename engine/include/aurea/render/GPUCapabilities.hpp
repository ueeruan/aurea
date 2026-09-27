// =============================================================================
//  Aurea / render / GPUCapabilities.hpp
//
//  O que a GPU deste aparelho sabe fazer — no vocabulário do motor, não no do
//  Vulkan nem no do Metal.
//
//  REGRA: toda pergunta "o aparelho suporta X?" é respondida UMA vez, na
//  inicialização do backend, e guardada aqui. O resto do motor lê esta struct.
//  Check espalhado (`if (vendor == ARM && driver < ...)`) é proibido: duas
//  partes do código acabam discordando sobre o mesmo aparelho, e o bug só
//  aparece num modelo específico que ninguém da equipe tem.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <string>
#include <vector>

namespace aurea {

enum class GpuDeviceType : u8 { Unknown = 0, Integrated, Discrete, Virtual, Cpu };

struct GpuMemoryHeap {
    u64  bytes = 0;
    bool deviceLocal = false;
};

struct GPUCapabilities {
    // --- Identidade -----------------------------------------------------------
    std::string apiName;            ///< "Vulkan", "Metal"
    u32 apiMajor = 0, apiMinor = 0, apiPatch = 0;
    std::string deviceName;
    std::string driverInfo;
    u32 vendorId = 0;
    u32 deviceId = 0;
    GpuDeviceType deviceType = GpuDeviceType::Unknown;

    // --- Aritmética e armazenamento -------------------------------------------
    bool fp16Arithmetic = false;    ///< float16 em shader (mediump de verdade)
    bool fp16Storage = false;       ///< buffers/push constants em 16 bits
    bool int16Arithmetic = false;

    // --- Limites --------------------------------------------------------------
    u32 maxTexture2D = 4096;
    u32 maxComputeWorkGroupInvocations = 128;
    u32 maxComputeWorkGroupSize[3] = {128, 128, 64};
    u32 maxComputeSharedMemoryBytes = 16384;
    u32 maxPushConstantBytes = 128;
    u32 maxUniformBufferRange = 16384;
    u32 maxBoundDescriptorSets = 4;
    u32 maxPerStageSampledImages = 16;
    u32 maxPerStageStorageImages = 4;
    u32 maxColorAttachments = 4;
    u32 minUniformBufferOffsetAlignment = 256;
    /// Bits de sample count suportados para cor (1 = 1x, 4 = 4x...). Máscara.
    u32 colorSampleCountMask = 1;
    /// O mesmo para profundidade. O MSAA do 3D usa o que as duas têm.
    u32 depthSampleCountMask = 1;
    /// Resolve da profundidade MSAA (amostra 0) no fim do passe: Vulkan
    /// VK_KHR_depth_stencil_resolve, Metal `depthAttachment.resolveTexture`.
    bool depthResolveSampleZero = false;
    /// Alpha-to-one junto do alpha-to-coverage (sem ele, o recorte por
    /// cobertura deixaria o alfa guardado = cobertura², ver SceneRenderer).
    bool alphaToOne = false;
    /// Memória que só existe no tile (Vulkan LAZILY_ALLOCATED, Metal
    /// memoryless): anexo MSAA transitório de graça em GPU móvel.
    bool lazyAttachments = false;

    /// Maior contagem de amostras ≤ `wanted` que cor e profundidade suportam
    /// (potência de 2; 1 quando não há MSAA).
    [[nodiscard]] u32 msaa_samples(u32 wanted) const noexcept {
        const u32 mask = colorSampleCountMask & depthSampleCountMask;
        for (u32 s = 8; s > 1; s >>= 1) {
            if (s <= wanted && (mask & s)) return s;
        }
        return 1;
    }

    // --- Tempo ----------------------------------------------------------------
    bool timestampQueries = false;  ///< mede GPU de verdade, por passe
    f32  timestampPeriodNs = 1.0f;

    // --- Formatos -------------------------------------------------------------
    bool rgba16fRenderable = false;
    bool rgba16fFilterable = false;
    bool rgba16fStorage = false;
    bool r16UnormSampled = false;   ///< planos de P010 enviados pela CPU

    // --- 3D ------------------------------------------------------------------
    f32  maxSamplerAnisotropy = 1.0f;   ///< 1 = sem filtro anisotrópico
    bool depth32fAttachment = false;
    bool depth24Attachment = false;
    bool depth32fSampled = false;       ///< mapa de sombra amostrável
    bool textureCompressionASTC = false;
    bool textureCompressionETC2 = false;
    bool textureCompressionBC = false;
    u32  maxVertexInputAttributes = 16;

    // --- Mídia (zero-copy) ----------------------------------------------------
    bool samplerYcbcrConversion = false;       ///< NV12/P010 amostrados direto
    bool externalMemoryHardwareBuffer = false; ///< AHardwareBuffer como textura
    bool queueFamilyForeign = false;           ///< posse transferida do decoder
    bool externalSemaphoreFd = false;

    // --- Memória --------------------------------------------------------------
    std::vector<GpuMemoryHeap> heaps;
    u64 deviceLocalBytes = 0;
    u64 hostVisibleBytes = 0;
    /// Memória unificada (celular): device-local e host-visible são a mesma.
    /// Muda a estratégia de upload — não há "copiar para a VRAM".
    bool unifiedMemory = false;

    // --- Diagnóstico ----------------------------------------------------------
    bool validationEnabled = false;
    std::vector<std::string> extensions;

    /// Pode fazer zero-copy de vídeo? As três peças precisam existir juntas.
    [[nodiscard]] bool zero_copy_video() const noexcept {
        return samplerYcbcrConversion && externalMemoryHardwareBuffer;
    }

    /// Resumo em uma linha, para log e painel.
    [[nodiscard]] std::string summary() const;
};

} // namespace aurea

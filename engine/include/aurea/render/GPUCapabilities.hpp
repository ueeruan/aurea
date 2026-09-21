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

    // --- Tempo ----------------------------------------------------------------
    bool timestampQueries = false;  ///< mede GPU de verdade, por passe
    f32  timestampPeriodNs = 1.0f;

    // --- Formatos -------------------------------------------------------------
    bool rgba16fRenderable = false;
    bool rgba16fFilterable = false;
    bool rgba16fStorage = false;
    bool r16UnormSampled = false;   ///< planos de P010 enviados pela CPU

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

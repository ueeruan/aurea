// =============================================================================
//  Aurea / gpu / metal / MetalBackend.hpp
//
//  O renderer do Aurea no iOS (e no macOS, para o host de desenvolvimento). O
//  MESMO FrameGraph, o MESMO EffectGraph, o MESMO Scene3D, o MESMO compositor e
//  o MESMO export que rodam sobre o Vulkan rodam sobre isto: aqui só existe a
//  tradução de `CommandList`/`GPUBackend` para Metal. Nada acima desta fronteira
//  sabe que este arquivo existe.
//
//  DIFERENÇAS DE SEMÂNTICA FRENTE AO VULKAN (as que importam, e por quê):
//
//   1. BARREIRA É AUTOMÁTICA. `CommandList::barrier()` só atualiza o estado
//      rastreado (para validar/diagnosticar). O Metal rastreia dependência de
//      recurso por conta própria: recursos criados pelo `MTLDevice` (não de
//      `MTLHeap`) têm hazard tracking e o driver insere a sincronização
//      necessária entre encoders e entre passes. Não existe `VkImageLayout`,
//      não existe transição de layout, e não existe o custo de uma barreira
//      emitida a mais. O motor continua chamando `barrier()` do mesmo jeito nos
//      dois backends — é o grafo que manda, e o backend que obedece.
//
//   2. NÃO EXISTE LAYOUT DE DESCRITOR. O layout universal do motor (o namespace
//      `binding::`) é mapeado para índices fixos de `buffer`/`texture`/`sampler`
//      do Metal (`slot::` abaixo). Como o mapeamento é fixo, não há
//      `MTLArgumentEncoder`, não há pool de descritores e não há
//      `allocate_set`: cada `bind_*` é uma chamada direta no encoder.
//
//   3. SAMPLER NÃO É IMUTÁVEL. `PipelineDesc::immutableSampler0` existe no
//      Vulkan porque a conversão YCbCr precisa ser gravada no layout do
//      pipeline. Em Metal o sampler é estado de draw, então o campo é ignorado
//      na criação do pipeline e o sampler do slot 0 é amarrado no draw, como
//      qualquer outro. Consequência prática: o cache de pipeline do motor (cuja
//      chave inclui `immutableSampler`) colapsa num pipeline único por formato
//      de vídeo, o que é o comportamento certo aqui.
//
//   4. NÃO HÁ PRÉ-ROTAÇÃO. `SurfaceRotation` é sempre `None`: no iOS quem gira a
//      layer é o sistema (a janela/CAMetalLayer já está na orientação da tela e
//      o UIKit entrega o tamanho já girado), então o passe final NÃO deve girar
//      de novo — girar seria girar duas vezes.
//
//   5. SHADER É MSL, EMBUTIDO NO BUILD. O mesmo SPIR-V do Android é traduzido
//      por SPIRV-Cross no build e o resultado (texto MSL, ou um `.metallib`
//      pré-compilado) entra no mesmo `ShaderBlob` que o SPIR-V entra no Android.
//      O formato do blob e a convenção do nome da função estão documentados em
//      `msl_glue.md`; o resumo está em `create_shader`.
//
//  COMO SE CRIA (SESSÃO A2 / camada de plataforma):
//
//      aurea::mtl::Backend* b = new aurea::mtl::Backend();
//      b->set_device(nullptr);                  // nullptr = MTLCreateSystemDefaultDevice()
//      b->initialize(config);                   // BackendConfig (cacheDirectory, timers…)
//      // ou, equivalente: GPUBackend* b = aurea::mtl::create_backend(device);
//
//  A ORDEM IMPORTA: `set_device` só vale ANTES de `initialize`. `create_backend`
//  é a forma canônica (cria a instância já com o device) — quem chama continua
//  responsável por `initialize(config)`, exatamente como no Vulkan.
// =============================================================================
#pragma once

#include "aurea/render/GPUBackend.hpp"

#include <memory>

namespace aurea::mtl {

// -----------------------------------------------------------------------------
// Tabela de slots → índices Metal. É a MESMA tabela dos dois lados (o motor
// declara no `namespace binding::`, o MSL recebe estes índices do SPIRV-Cross).
//
//   binding do motor                     namespace Metal   índice
//   ---------------------------------------------------------------------------
//   binding::kTextureSlots   0..11       texture            0..11
//   (o sampler de cada slot)             sampler            0..11
//   binding::kUniform        12          buffer            12
//   binding::kStorageImage0  13          texture           13
//   binding::kStorageImage1  14          texture           14
//   binding::kStorageBuffer  15          buffer            15
//   binding::kStorageBuffer1 16          buffer            16
//   push constants (≤128 B)              buffer            30
//
// O índice é IGUAL ao binding do Vulkan: um shader traduzido não precisa de
// remapeamento nenhum, e um dump de captura do Xcode mostra o mesmo número que
// o `bindings.glsl` declara. O buraco em 12 (texture) é de propósito: preferir
// densidade seria inventar uma segunda tabela para manter em sincronia.
//
// Índices 31 e acima NÃO são usados: `setVertexBuffer:atIndex:` compartilha o
// espaço de buffers com os argumentos de função, e o Metal reserva a faixa
// alta para o buffer de argumentos quando ele existe.
// -----------------------------------------------------------------------------
namespace slot {
    inline constexpr u32 kTexture0         = 0;                          ///< = binding::kTextureSlots (início)
    inline constexpr u32 kTextureSlots     = binding::kTextureSlots;      ///< 12: texturas amostradas
    inline constexpr u32 kUniform          = binding::kUniform;           ///< 12: bloco de parâmetros do passe
    inline constexpr u32 kStorageImage0    = binding::kStorageImage0;     ///< 13: primeira imagem de compute
    inline constexpr u32 kStorageImageSlots = binding::kStorageImageSlots;///< 2
    inline constexpr u32 kStorageBuffer    = binding::kStorageBuffer;     ///< 15: dado principal
    inline constexpr u32 kStorageBuffer1   = binding::kStorageBuffer1;    ///< 16: dado auxiliar (histórico de partículas)
    inline constexpr u32 kBindingCount     = binding::kBindingCount;      ///< 17
    /// `setBytes:length:atIndex:` dos push constants. O MSL declara o bloco como
    /// `constant ...& [[buffer(30)]]` (ver msl_glue.md; o SPIRV-Cross emite isso
    /// sozinho — se a versão usada emitir outro índice, é ESTE o número a
    /// ajustar, e nada mais).
    inline constexpr u32 kPushConstant     = 30;
    /// Alinhamento de todo deslocamento dentro do anel de uniforms. 256 é o que
    /// o Metal exige para um buffer amarrado como bloco de constantes.
    inline constexpr u32 kRingAlignment    = 256;
    [[nodiscard]] constexpr u32 texture_index(u32 slotIndex) noexcept { return slotIndex; }
    [[nodiscard]] constexpr u32 sampler_index(u32 slotIndex) noexcept { return slotIndex; }
} // namespace slot

/// Teto de marcas de tempo por frame (o mesmo do Vulkan: um passe chega a
/// gerar duas, e 50 camadas com efeitos passam de 190).
inline constexpr u32 kMaxTimers = 512;

class Backend;

/// Estado interno do backend (Metal, Objective-C++, `MetalInternal.hpp`). Fica
/// como `unique_ptr` de tipo incompleto para que ESTE header continue sendo C++
/// puro: a camada de plataforma e o host de testes o incluem sem Metal.
struct Impl;

/// Cria o backend Metal. `device` é um `id<MTLDevice>` (void* para não vazar
/// Objective-C no header público do motor); nullptr = MTLCreateSystemDefaultDevice().
/// A instância volta SEM `initialize`: quem chama passa a `BackendConfig` (mesmo
/// contrato do Vulkan, onde a plataforma também cria e inicializa).
[[nodiscard]] GPUBackend* create_backend(void* device = nullptr) noexcept;

/// O backend. Construível por `new aurea::mtl::Backend()` sem argumentos (igual
/// ao Vulkan); o device entra por `set_device` antes de `initialize`, ou por
/// `create_backend`.
class Backend final : public GPUBackend {
public:
    Backend();
    ~Backend() override;

    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    /// Device explícito (`id<MTLDevice>` como void*). `nullptr` = o device padrão
    /// do sistema. Chamar DEPOIS de `initialize` não tem efeito (log de aviso).
    void set_device(void* mtlDevice) noexcept;

    [[nodiscard]] const char* name() const noexcept override;
    [[nodiscard]] const GPUCapabilities& capabilities() const noexcept override;

    [[nodiscard]] Status initialize(const BackendConfig& config) noexcept override;
    void shutdown() noexcept override;

    [[nodiscard]] Status attach_surface(const SurfaceDesc& desc) noexcept override;
    void detach_surface() noexcept override;
    [[nodiscard]] Status resize_surface(u32 width, u32 height) noexcept override;
    [[nodiscard]] bool has_surface() const noexcept override;

    [[nodiscard]] Status begin_frame(FrameBegin& out) noexcept override;
    [[nodiscard]] Status end_frame() noexcept override;
    [[nodiscard]] Status begin_offscreen_frame(FrameBegin& out) noexcept override;

    [[nodiscard]] Result<TextureHandle>  create_texture(const TextureDesc& desc) noexcept override;
    [[nodiscard]] Result<BufferHandle>   create_buffer(const BufferDesc& desc) noexcept override;
    [[nodiscard]] Result<SamplerHandle>  create_sampler(const SamplerDesc& desc) noexcept override;
    [[nodiscard]] Result<ShaderHandle>   create_shader(const ShaderDesc& desc) noexcept override;
    [[nodiscard]] Result<PipelineHandle> create_pipeline(const PipelineDesc& desc) noexcept override;

    void destroy_texture(TextureHandle h) noexcept override;
    void destroy_buffer(BufferHandle h) noexcept override;
    void destroy_sampler(SamplerHandle h) noexcept override;
    void destroy_shader(ShaderHandle h) noexcept override;
    void destroy_pipeline(PipelineHandle h) noexcept override;

    [[nodiscard]] TextureDesc texture_desc(TextureHandle h) const noexcept override;

    [[nodiscard]] Status upload_texture(TextureHandle dst, const void* data, u32 bytesPerRow) noexcept override;
    [[nodiscard]] Status write_buffer(BufferHandle dst, usize offset, const void* data, usize bytes) noexcept override;
    [[nodiscard]] Status map_buffer(BufferHandle buffer, void*& outPtr) noexcept override;
    void unmap_buffer(BufferHandle buffer) noexcept override;
    [[nodiscard]] Status upload_texture_level(TextureHandle dst, u32 mipLevel, u32 layer, const void* data,
                                              usize bytes) noexcept override;
    [[nodiscard]] Status generate_mipmaps(TextureHandle texture) noexcept override;
    [[nodiscard]] Status read_texture(TextureHandle src, void* outData, u32 bytesPerRow) noexcept override;

    // --- Zero-copy de mídia ---------------------------------------------------
    //
    // `ExternalImageDesc::nativeHandle` é um `CVPixelBufferRef` do VideoToolbox.
    // A importação é zero-copy de verdade: `CVMetalTextureCache` embrulha o
    // IOSurface do buffer numa `MTLTexture`, sem copiar plano nenhum.
    //
    // CONVERSÃO YCbCr — A DIFERENÇA HONESTA FRENTE AO VULKAN:
    //
    //   No Vulkan, a matriz e a faixa do arquivo entram no
    //   `VkSamplerYcbcrConversion` e o sampler entrega R'G'B' já convertido; o
    //   motor recebe `ExternalTexture::rgb = true` e o shader só decodifica a
    //   curva de transferência e as primárias.
    //
    //   Em Metal a textura é criada com um `MTLPixelFormat*YpCbCr*` biplanar
    //   (o formato cobre os DOIS planos numa textura só) e a amostra TAMBÉM sai
    //   convertida — mas pela matriz do FORMATO, que é fixa: BT.601 nas
    //   variantes normais e BT.709 nas `*_sRGB` (que ainda aplicam a curva sRGB
    //   por cima). Não existe, em Metal, um jeito de o sampler honrar a matriz
    //   do arquivo.
    //
    //   Ou seja: aqui o sampler converte igual ao Vulkan, e por isso o
    //   `ExternalTexture` deste backend sai com `rgb = true` — devolver
    //   `rgb = false` faria o shader do motor aplicar a matriz YCbCr EM CIMA de
    //   uma amostra que já é RGB (conversão dupla: verde e magenta). O shader do
    //   motor NÃO precisa de mudança nenhuma: o ramo `sampling.w > 0.5`
    //   (`s.rgb`) é exatamente o certo para esta amostra.
    //
    //   Limite conhecido e assumido: um arquivo BT.709/BT.2020 amostrado por um
    //   formato BT.601 sai com a matriz trocada (o mesmo desvio de um player que
    //   ignora os metadados). Corrigir de verdade exige a conversão no shader a
    //   partir dos DOIS planos crus (Y e CbCr), e isso exigiria dois
    //   `TextureHandle` no `ExternalTexture` — mudança na interface do motor,
    //   fora do escopo deste backend. Quem quiser o caminho exato hoje: pedir o
    //   quadro em `kCVPixelFormatType_32BGRA` ao VideoToolbox (o backend importa
    //   BGRA direto, sem conversão nenhuma). Ver README_CONTRATO.md.
    [[nodiscard]] Result<ExternalTexture> import_external_image(
        const ExternalImageDesc& img) noexcept override;
    void release_external_image(TextureHandle imported) noexcept override;

    void defer_until_gpu_done(void (*fn)(void*), void* ctx) noexcept override;

    void wait_idle() noexcept override;
    [[nodiscard]] u64 last_submitted_frame() const noexcept override;
    [[nodiscard]] Status wait_frame(u64 frameNumber, u64 timeoutNs) noexcept override;
    [[nodiscard]] u32 read_gpu_timings(GpuTiming* out, u32 capacity, f32* totalMs) noexcept override;
    [[nodiscard]] bool is_device_lost() const noexcept override;
    [[nodiscard]] u32 frames_in_flight() const noexcept override;
    [[nodiscard]] GpuMemoryStats memory_stats() const noexcept override;

    void save_pipeline_cache() noexcept override;
    [[nodiscard]] PipelineCacheInfo pipeline_cache_info() const noexcept override;

    // --- Internos, usados pelas peças do backend (MetalInternal.hpp) ----------
    // Único ponto que liga o contrato público ao estado Objective-C++.
    [[nodiscard]] Impl& impl() noexcept;
    [[nodiscard]] const Impl& impl() const noexcept;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace aurea::mtl

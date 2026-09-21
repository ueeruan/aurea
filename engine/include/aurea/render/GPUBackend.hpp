// =============================================================================
//  Aurea / render / GPUBackend.hpp
//
//  A fronteira entre o motor e a API gráfica.
//
//  REGRA: nada acima desta interface menciona Vulkan ou Metal. O FrameGraph, o
//  compositor, os efeitos e o export falam em `TextureHandle`, `PipelineHandle`
//  e `CommandList`. O backend Vulkan (Android, e o host de testes) e o Metal
//  (iOS, próxima fase) implementam isto — e o mesmo código de composição roda
//  nos dois. Preview e export idênticos são consequência da arquitetura.
//
//  AS TRÊS DECISÕES QUE MOLDAM A INTERFACE:
//
//   1. SHADER É SPIR-V COMPILADO NO BUILD. Não existe compilação de GLSL em
//      runtime: o custo (dezenas de ms) cairia no meio do playback. O Metal
//      recebe MSL gerado do mesmo SPIR-V, também no build.
//
//   2. LAYOUT DE RECURSOS UNIVERSAL. Todo pipeline do Aurea enxerga o mesmo
//      conjunto de slots (ver `binding::`). Um efeito novo não cria layout de
//      descritor novo, e o descritor de um passe é montado por slot, sem o
//      motor saber o que é `VkDescriptorSetLayout`.
//
//   3. QUEM DECIDE A BARREIRA É O FRAMEGRAPH; QUEM CONHECE O ESTADO É O
//      BACKEND. O grafo diz "esta textura agora vai ser lida em shader"; o
//      backend sabe em que layout ela está e emite a transição mínima. Assim
//      o grafo não precisa saber de `VkImageLayout`, e o backend não precisa
//      entender dependências entre passes.
// =============================================================================
#pragma once

#include "aurea/media/VideoTypes.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/core/Result.hpp"
#include "aurea/core/Math.hpp"
#include "aurea/render/GPUCapabilities.hpp"

namespace aurea {

// -----------------------------------------------------------------------------
// Handles opacos. Nada aqui é ponteiro: um handle pode ser validado e
// sobrevive à recriação do swapchain.
// -----------------------------------------------------------------------------
template <typename Tag>
struct GpuHandle {
    u64 id = 0;
    [[nodiscard]] bool valid() const noexcept { return id != 0; }
    friend constexpr bool operator==(GpuHandle, GpuHandle) noexcept = default;
};

struct TextureTag;
struct BufferTag;
struct PipelineTag;
struct SamplerTag;
struct ShaderTag;

using TextureHandle  = GpuHandle<TextureTag>;
using BufferHandle   = GpuHandle<BufferTag>;
using PipelineHandle = GpuHandle<PipelineTag>;
using SamplerHandle  = GpuHandle<SamplerTag>;
using ShaderHandle   = GpuHandle<ShaderTag>;

// -----------------------------------------------------------------------------
// Layout universal de recursos.
//
// Todo shader do Aurea declara (no máximo) estes slots, no set 0:
//
//   binding 0..3  sampler2D     u_tex0..u_tex3   (entrada, máscara, LUT...)
//   binding 4     uniform block u_params          (parâmetros do passe)
//   binding 5..6  image2D       u_img0..u_img1   (saída de compute)
//   binding 7     buffer        u_data            (reduções, histograma)
//
//   push constants: até 128 bytes (o mínimo garantido pelo Vulkan).
//
// O `common/bindings.glsl` declara exatamente isto — é a mesma tabela dos dois
// lados, e mudar um sem o outro quebra na validação, não em produção.
// -----------------------------------------------------------------------------
namespace binding {
    inline constexpr u32 kTextureSlots      = 4;
    inline constexpr u32 kUniform           = 4;
    inline constexpr u32 kStorageImage0     = 5;
    inline constexpr u32 kStorageImageSlots = 2;
    inline constexpr u32 kStorageBuffer     = 7;
    inline constexpr u32 kPushConstantBytes = 128;
    /// Tamanho máximo de um bloco de uniform por passe. 1 KB cobre a matriz de
    /// cor de 16 operações fundidas com folga; passar disso é sinal de que o
    /// dado deveria ser textura ou storage buffer.
    inline constexpr u32 kMaxUniformBytes   = 1024;
}

// -----------------------------------------------------------------------------
// Estado de uso de um recurso. O FrameGraph pede; o backend traduz em layout
// e barreira.
// -----------------------------------------------------------------------------
enum class ResourceState : u8 {
    Undefined = 0,
    ShaderRead,        ///< amostrada em fragment ou compute
    ColorAttachment,   ///< alvo de render pass
    StorageWrite,      ///< escrita de compute (image2D)
    StorageReadWrite,
    TransferSrc,
    TransferDst,
    Present,           ///< pronta para o swapchain
};

enum class LoadOp : u8 {
    Clear = 0,
    Load,
    DontCare,   ///< o passe cobre o alvo inteiro: não pagar leitura de memória
};

// -----------------------------------------------------------------------------
// Textura
// -----------------------------------------------------------------------------
struct TextureDesc {
    u32 width  = 0;
    u32 height = 0;
    u32 depth  = 1;
    u32 layers = 1;
    u32 mipLevels = 1;
    SurfaceFormat format = SurfaceFormat::RGBA8;
    u32 sampleCount = 1;

    /// Uso declarado. O backend escolhe memória e flags a partir disto.
    bool sampled      = true;
    bool renderTarget = false;
    bool storage      = false;
    bool transferSrc  = false;
    bool transferDst  = false;

    /// Nome para validação/depuração. Não entra na identidade da textura.
    const char* debugName = nullptr;

    [[nodiscard]] bool is_depth() const noexcept {
        return format == SurfaceFormat::Depth24 || format == SurfaceFormat::Depth32F;
    }
    [[nodiscard]] u32 bytes_per_pixel() const noexcept { return surface_format_bytes(format); }
    [[nodiscard]] u64 estimated_bytes() const noexcept {
        return static_cast<u64>(width) * height * depth * layers * bytes_per_pixel() * sampleCount;
    }

    /// Duas descrições são intercambiáveis para o pool de texturas quando
    /// forma, formato e uso batem. O nome não conta.
    [[nodiscard]] bool compatible(const TextureDesc& o) const noexcept {
        return width == o.width && height == o.height && depth == o.depth
            && layers == o.layers && mipLevels == o.mipLevels && format == o.format
            && sampleCount == o.sampleCount && sampled == o.sampled
            && renderTarget == o.renderTarget && storage == o.storage
            && transferSrc == o.transferSrc && transferDst == o.transferDst;
    }

    [[nodiscard]] static constexpr u32 surface_format_bytes(SurfaceFormat f) noexcept {
        switch (f) {
            case SurfaceFormat::R8:       return 1;
            case SurfaceFormat::RG8:      return 2;
            case SurfaceFormat::RGBA8:    return 4;
            case SurfaceFormat::BGRA8:    return 4;
            case SurfaceFormat::R16F:     return 2;
            case SurfaceFormat::R16:      return 2;
            case SurfaceFormat::RG16:     return 4;
            case SurfaceFormat::RG16F:    return 4;
            case SurfaceFormat::RGBA16F:  return 8;
            case SurfaceFormat::R32F:     return 4;
            case SurfaceFormat::RGBA32F:  return 16;
            case SurfaceFormat::Depth24:  return 4;
            case SurfaceFormat::Depth32F: return 4;
        }
        return 4;
    }
};

// -----------------------------------------------------------------------------
// Buffer
// -----------------------------------------------------------------------------
enum class BufferUsage : u32 {
    None     = 0,
    Uniform  = 1u << 0,
    Storage  = 1u << 1,
    Vertex   = 1u << 2,
    Index    = 1u << 3,
    Indirect = 1u << 4,
    TransferSrc = 1u << 5,
    TransferDst = 1u << 6,
};
[[nodiscard]] constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept {
    return static_cast<BufferUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}
[[nodiscard]] constexpr bool has_flag(BufferUsage set, BufferUsage flag) noexcept {
    return (static_cast<u32>(set) & static_cast<u32>(flag)) != 0;
}

/// Quem acessa a memória do buffer.
enum class MemoryAccess : u8 {
    GpuOnly = 0,   ///< só a GPU; subir dado exige staging
    Upload,        ///< a CPU escreve, a GPU lê (mapeado persistente)
    Readback,      ///< a GPU escreve, a CPU lê (export, testes)
};

struct BufferDesc {
    usize bytes = 0;
    BufferUsage usage = BufferUsage::Storage;
    MemoryAccess access = MemoryAccess::GpuOnly;
    const char* debugName = nullptr;
};

// -----------------------------------------------------------------------------
// Sampler
// -----------------------------------------------------------------------------
struct SamplerDesc {
    enum class Filter : u8 { Nearest = 0, Linear };
    enum class Mipmap : u8 { Nearest = 0, Linear };
    enum class Wrap : u8 { Repeat = 0, ClampToEdge, MirroredRepeat, ClampToBorder };

    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    Mipmap mipmap    = Mipmap::Nearest;
    Wrap   wrapU = Wrap::ClampToEdge;
    Wrap   wrapV = Wrap::ClampToEdge;
    /// Borda transparente (0,0,0,0) quando `ClampToBorder`. É o que efeito de
    /// vizinhança usa para não "esticar" a última coluna da imagem.
    f32    maxAnisotropy = 1.0f;

    friend constexpr bool operator==(const SamplerDesc&, const SamplerDesc&) noexcept = default;
};

// -----------------------------------------------------------------------------
// Shader e pipeline
// -----------------------------------------------------------------------------
enum class ShaderStage : u8 { Vertex = 0, Fragment, Compute };

struct ShaderDesc {
    ShaderStage stage = ShaderStage::Fragment;
    /// SPIR-V gerado no build (glslc). Nunca GLSL em runtime.
    const u32* spirv = nullptr;
    usize      spirvBytes = 0;
    const char* entryPoint = "main";
    const char* debugName = nullptr;
};

enum class Topology : u8 { TriangleList = 0, TriangleStrip, PointList, LineList };

struct PipelineDesc {
    bool isCompute = false;

    ShaderHandle vertexShader{};     ///< ignorado quando isCompute
    ShaderHandle fragmentShader{};   ///< ignorado quando isCompute
    ShaderHandle computeShader{};    ///< usado só quando isCompute

    /// Blend de hardware. Todo alvo do Aurea é PRÉ-MULTIPLICADO: `Normal` é
    /// `src + dst*(1-srcA)`. Modos que não cabem em blend fixo não passam por
    /// aqui — o compositor os faz em shader com leitura do destino.
    bool      blendEnabled = false;
    BlendMode blend = BlendMode::Normal;

    SurfaceFormat colorFormat = SurfaceFormat::RGBA16F;
    Topology topology = Topology::TriangleList;

    /// Sampler imutável no slot 0. É como a conversão YCbCr entra: o Vulkan
    /// exige que o sampler de uma imagem externa (NV12/P010 do decoder) seja
    /// gravado no layout do pipeline. O handle vem de `import_external_image`.
    SamplerHandle immutableSampler0{};

    const char* debugName = nullptr;
};

// -----------------------------------------------------------------------------
// Imagem externa — o frame do decoder, sem cópia.
// -----------------------------------------------------------------------------
struct ExternalImageDesc {
    /// AHardwareBuffer* no Android, CVPixelBufferRef no iOS.
    void* nativeHandle = nullptr;
    u32 width = 0;
    u32 height = 0;
    PixelFormat format = PixelFormat::Opaque;
    /// Matriz e faixa DO ARQUIVO. A conversão YCbCr do sampler usa estes
    /// valores, nunca a sugestão do gralloc (que muitos aparelhos, e o
    /// emulador, preenchem errado).
    YCbCrMatrix matrix = YCbCrMatrix::BT709;
    bool fullRange = false;
};

/// Resultado de uma importação. `sampler` é o sampler de conversão YCbCr que
/// o pipeline de leitura precisa receber em `immutableSampler0`; `formatKey`
/// identifica o formato externo, para o chamador cachear o pipeline certo.
struct ExternalTexture {
    TextureHandle texture{};
    SamplerHandle sampler{};
    u64 formatKey = 0;
    /// A amostra já sai em RGB não linear (o sampler converteu com a matriz do
    /// arquivo, ou o buffer já era RGB): o shader só decodifica curva e
    /// primárias.
    bool rgb = false;
};

// -----------------------------------------------------------------------------
// Superfície de apresentação
// -----------------------------------------------------------------------------
struct SurfaceDesc {
    void* nativeWindow = nullptr;   ///< ANativeWindow* / CAMetalLayer*
    u32   width  = 0;
    u32   height = 0;
    /// VSync. O preview SEMPRE usa: desenhar frames que o display não mostra
    /// só gasta bateria e esquenta o aparelho.
    bool  vsync = true;
};

/// Rotação que a superfície pede. No Android o compositor do sistema giraria
/// a imagem (custo extra por frame) se o app não fizesse a pré-rotação; o
/// backend informa, e o passe final gira no shader.
enum class SurfaceRotation : u8 { None = 0, Rotate90, Rotate180, Rotate270 };

struct BackendConfig {
    /// Camadas de validação. Ligadas em debug, desligadas em release.
    bool enableValidation = false;
    /// Onde persistir o cache de pipeline entre execuções.
    const char* cacheDirectory = nullptr;
    /// Frames em voo (CPU gravando o N+1 enquanto a GPU executa o N).
    u32 framesInFlight = 3;
    /// Liga as timestamp queries por passe (painel DEV).
    bool enableGpuTimers = true;
};

/// Um tempo de GPU medido de um frame já concluído.
struct GpuTiming {
    const char* label = nullptr;   ///< string estática do passe
    u32 id = 0;
    f32 ms = 0.0f;
};

/// Estatística de memória do backend, para o painel.
struct GpuMemoryStats {
    u64 reservedBytes = 0;     ///< blocos pedidos ao driver
    u64 usedBytes = 0;         ///< ocupado por recursos vivos
    u32 blockCount = 0;
    u32 allocationCount = 0;
    u32 textureCount = 0;
    u32 bufferCount = 0;
    u64 uploadBytesThisFrame = 0;
};

// -----------------------------------------------------------------------------
// Lista de comandos do frame. O backend grava; o motor só descreve.
// -----------------------------------------------------------------------------
struct RenderPassBegin {
    TextureHandle color{};
    LoadOp  load = LoadOp::Clear;
    f32     clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

class CommandList {
public:
    virtual ~CommandList() = default;

    /// Transição de estado. O backend sabe o estado atual e emite o mínimo;
    /// `discard` diz que o conteúdo anterior não importa (primeira escrita de
    /// um recurso transitório) — em GPU tile-based isso economiza uma leitura
    /// inteira da textura.
    virtual void barrier(TextureHandle texture, ResourceState newState,
                         bool discard = false) noexcept = 0;

    virtual void begin_render_pass(const RenderPassBegin& pass) noexcept = 0;
    virtual void end_render_pass() noexcept = 0;

    virtual void bind_pipeline(PipelineHandle pipeline) noexcept = 0;
    virtual void bind_texture(u32 slot, TextureHandle texture, SamplerHandle sampler) noexcept = 0;
    virtual void bind_storage_image(u32 slot, TextureHandle texture) noexcept = 0;
    virtual void bind_storage_buffer(BufferHandle buffer) noexcept = 0;

    /// Bloco de parâmetros do passe. Copiado para o anel de uniforms do frame
    /// — o chamador pode reusar a memória logo em seguida.
    virtual void set_uniforms(const void* data, u32 bytes) noexcept = 0;
    virtual void push_constants(const void* data, u32 bytes) noexcept = 0;

    /// Viewport em pixels do alvo. Sem chamada, vale o alvo inteiro.
    virtual void set_viewport(f32 x, f32 y, f32 w, f32 h) noexcept = 0;
    virtual void set_scissor(i32 x, i32 y, u32 w, u32 h) noexcept = 0;

    virtual void draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0) noexcept = 0;
    virtual void dispatch(u32 groupsX, u32 groupsY, u32 groupsZ) noexcept = 0;

    virtual void copy_texture(TextureHandle src, TextureHandle dst) noexcept = 0;
    virtual void copy_texture_to_buffer(TextureHandle src, BufferHandle dst) noexcept = 0;

    /// Marca de tempo por passe. `label` precisa ser string estática: o
    /// resultado só volta alguns frames depois, quando a GPU terminou.
    virtual void begin_timer(const char* label) noexcept = 0;
    virtual void end_timer() noexcept = 0;

    /// Rótulo para RenderDoc/validação. Sem custo quando não há depurador.
    virtual void begin_label(const char* label) noexcept = 0;
    virtual void end_label() noexcept = 0;
};

// -----------------------------------------------------------------------------
// Frame
// -----------------------------------------------------------------------------
struct FrameBegin {
    CommandList* commands = nullptr;
    /// Imagem do swapchain, ou inválido quando não há superfície (export,
    /// testes, app em segundo plano).
    TextureHandle backbuffer{};
    u32 backbufferWidth = 0;
    u32 backbufferHeight = 0;
    SurfaceFormat backbufferFormat = SurfaceFormat::RGBA8;
    SurfaceRotation rotation = SurfaceRotation::None;
    /// Número monotônico do frame.
    u64 frameNumber = 0;
};

// -----------------------------------------------------------------------------
// Backend
// -----------------------------------------------------------------------------
class GPUBackend {
public:
    virtual ~GPUBackend() = default;

    [[nodiscard]] virtual const char* name() const noexcept = 0;
    [[nodiscard]] virtual const GPUCapabilities& capabilities() const noexcept = 0;

    // --- Ciclo de vida --------------------------------------------------------
    [[nodiscard]] virtual Status initialize(const BackendConfig& config) noexcept = 0;
    virtual void shutdown() noexcept = 0;

    // --- Superfície -----------------------------------------------------------
    //
    // O Android destrói a superfície ao ir para segundo plano e entrega OUTRA
    // ao voltar. Só o swapchain é refeito: pipelines, texturas e caches
    // sobrevivem, e é por isso que voltar do background é instantâneo.
    [[nodiscard]] virtual Status attach_surface(const SurfaceDesc& desc) noexcept = 0;
    virtual void detach_surface() noexcept = 0;
    [[nodiscard]] virtual Status resize_surface(u32 width, u32 height) noexcept = 0;
    [[nodiscard]] virtual bool has_surface() const noexcept = 0;

    // --- Frame ----------------------------------------------------------------
    /// Espera o contexto de frame mais antigo ficar livre (fence) e começa a
    /// gravar. Com superfície, adquire a imagem do swapchain. Devolve
    /// `SurfaceLost` quando o swapchain precisa ser refeito e não pôde ser.
    [[nodiscard]] virtual Status begin_frame(FrameBegin& out) noexcept = 0;
    /// Submete e, se houver backbuffer, apresenta.
    [[nodiscard]] virtual Status end_frame() noexcept = 0;

    // --- Recursos -------------------------------------------------------------
    [[nodiscard]] virtual Result<TextureHandle>  create_texture(const TextureDesc&) noexcept = 0;
    [[nodiscard]] virtual Result<BufferHandle>   create_buffer(const BufferDesc&) noexcept = 0;
    [[nodiscard]] virtual Result<SamplerHandle>  create_sampler(const SamplerDesc&) noexcept = 0;
    [[nodiscard]] virtual Result<ShaderHandle>   create_shader(const ShaderDesc&) noexcept = 0;
    [[nodiscard]] virtual Result<PipelineHandle> create_pipeline(const PipelineDesc&) noexcept = 0;

    /// Destruição ADIADA: o objeto só morre quando a GPU terminou todo frame
    /// que pode tê-lo usado. O handle fica inválido na hora.
    virtual void destroy_texture(TextureHandle) noexcept = 0;
    virtual void destroy_buffer(BufferHandle) noexcept = 0;
    virtual void destroy_sampler(SamplerHandle) noexcept = 0;
    virtual void destroy_shader(ShaderHandle) noexcept = 0;
    virtual void destroy_pipeline(PipelineHandle) noexcept = 0;

    [[nodiscard]] virtual TextureDesc texture_desc(TextureHandle) const noexcept = 0;

    // --- Dados ----------------------------------------------------------------
    /// Sobe pixels para uma textura (imagem importada, LUT, fallback de vídeo
    /// sem zero-copy). Grava no fluxo de upload do frame atual se houver frame
    /// aberto; caso contrário, submete e espera.
    [[nodiscard]] virtual Status upload_texture(TextureHandle dst, const void* data,
                                                u32 bytesPerRow) noexcept = 0;
    [[nodiscard]] virtual Status write_buffer(BufferHandle dst, usize offset,
                                              const void* data, usize bytes) noexcept = 0;
    [[nodiscard]] virtual Status map_buffer(BufferHandle buffer, void*& outPtr) noexcept = 0;
    virtual void unmap_buffer(BufferHandle buffer) noexcept = 0;

    /// Lê uma textura de volta para a CPU. Síncrono e caro — testes visuais e
    /// export sem caminho de superfície. Nunca no preview.
    [[nodiscard]] virtual Status read_texture(TextureHandle src, void* outData,
                                              u32 bytesPerRow) noexcept = 0;

    // --- Zero-copy de mídia ---------------------------------------------------
    [[nodiscard]] virtual Result<ExternalTexture> import_external_image(
        const ExternalImageDesc& img) noexcept = 0;
    /// Libera a textura importada (adiado até a GPU terminar de lê-la).
    virtual void release_external_image(TextureHandle imported) noexcept = 0;

    /// Executa `fn(ctx)` quando a GPU concluir o frame que está sendo gravado
    /// agora. É como a camada de mídia devolve o buffer ao decoder no momento
    /// certo: nem antes (frame rasgado), nem muito depois (decoder travado).
    virtual void defer_until_gpu_done(void (*fn)(void*), void* ctx) noexcept = 0;

    // --- Sincronização e medição ----------------------------------------------
    virtual void wait_idle() noexcept = 0;

    /// Tempos de GPU do frame mais recente já concluído. `out` é preenchido
    /// até `capacity`; devolve quantos. Zero quando o aparelho não mede.
    [[nodiscard]] virtual u32 read_gpu_timings(GpuTiming* out, u32 capacity,
                                               f32* totalMs) noexcept = 0;

    [[nodiscard]] virtual bool is_device_lost() const noexcept = 0;
    [[nodiscard]] virtual u32  frames_in_flight() const noexcept = 0;
    [[nodiscard]] virtual GpuMemoryStats memory_stats() const noexcept = 0;

    /// Grava o cache de pipeline. Chamado ao ir para segundo plano — pode
    /// bloquear enquanto o driver serializa, então NUNCA no caminho de frame.
    virtual void save_pipeline_cache() noexcept = 0;
};

// Não há `create_default()` aqui de propósito: o núcleo não pode depender de
// Vulkan nem de Metal para compilar. Quem cria o backend é a camada de
// plataforma (JNI, ObjC++, o executável de teste) e o entrega ao `Engine`.

} // namespace aurea

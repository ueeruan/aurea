// =============================================================================
//  Aurea / render / GPUBackend.hpp
//
//  A fronteira entre o motor e a API gráfica.
//
//  REGRA: nada acima desta interface menciona Vulkan ou Metal. O compositor, o
//  grafo de efeitos, o motor 3D e o export falam em termos de `TextureHandle`,
//  `PipelineHandle` e `CommandList`. Trocar de backend, ou rodar os dois no
//  mesmo build (Android com fallback GLES), não toca em nenhuma linha de cima.
//
//  Consequência prática: o MESMO código de composição roda no preview Android
//  (Vulkan) e no preview iOS (Metal). Preview e export serem visualmente
//  idênticos deixa de depender de disciplina e passa a ser consequência da
//  arquitetura — só existe uma implementação.
//
//  Sobre zero-copy: o ponto de entrada de mídia é uma imagem externa
//  (`ExternalImageHandle`), que no Android é um AHardwareBuffer vindo do
//  MediaCodec e no iOS um CVPixelBuffer vindo do VideoToolbox. O backend sabe
//  importar isso como textura SEM passar pelo CPU. É o que evita o
//  decode → bitmap → UI → GPU que a arquitetura proíbe.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Result.hpp"
#include "aurea/core/Math.hpp"

#include <string>

namespace aurea {

// -----------------------------------------------------------------------------
// Handles opacos. Nada aqui é ponteiro: um handle pode ser serializado e
// validado, e sobrevive à recriação de um swapchain.
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
struct RenderPassTag;
struct ShaderTag;
struct QueryPoolTag;
struct FenceTag;

using TextureHandle  = GpuHandle<TextureTag>;
using BufferHandle   = GpuHandle<BufferTag>;
using PipelineHandle = GpuHandle<PipelineTag>;
using SamplerHandle  = GpuHandle<SamplerTag>;
using RenderPassHandle = GpuHandle<RenderPassTag>;
using ShaderHandle   = GpuHandle<ShaderTag>;
using QueryPoolHandle = GpuHandle<QueryPoolTag>;
using FenceHandle    = GpuHandle<FenceTag>;

/// Imagem vinda de fora do motor — o frame decodificado pelo hardware.
/// `nativeHandle` é AHardwareBuffer* no Android e CVPixelBufferRef no iOS.
/// O motor só repassa; quem cria e destrói é a camada de mídia da plataforma.
struct ExternalImageHandle {
    void* nativeHandle = nullptr;
    u32   width = 0;
    u32   height = 0;
    PixelFormat format = PixelFormat::NV12;
    u32   planeCount = 1;
    /// Timestamp de apresentação do frame, em unidades do container.
    i64   presentationTime = 0;
    u32   timescale = 1000;

    [[nodiscard]] bool valid() const noexcept { return nativeHandle != nullptr; }
};

// -----------------------------------------------------------------------------
// Textura
// -----------------------------------------------------------------------------
struct TextureDesc {
    u32          width  = 0;
    u32          height = 0;
    u32          depth  = 1;
    u32          layers = 1;
    u32          mipLevels = 1;
    SurfaceFormat format = SurfaceFormat::RGBA8;
    u32          sampleCount = 1;

    /// Uso. Declarado explicitamente porque o backend escolhe a alocação com
    /// base nisto: uma textura só amostrada vai para memória local do device;
    /// uma que o decoder escreve precisa ser exportável.
    bool sampled      = true;
    bool renderTarget = false;
    bool storage      = false;
    bool transferSrc  = false;
    bool transferDst  = false;
    bool externalImport = false;   ///< recebe imagem de fora (zero-copy)

    /// Uso de buffer. Só significativo quando a descrição descreve um buffer
    /// (o FrameGraph usa o mesmo tipo para os dois). Valores são bits livres
    /// definidos por quem cria: vértice, índice, uniform, storage, indireto.
    u32 usageHint = 0;

    /// Nome para o painel de telemetria. Não vai para o driver.
    const char* debugName = nullptr;

    [[nodiscard]] bool is_depth() const noexcept {
        return format == SurfaceFormat::Depth24 || format == SurfaceFormat::Depth32F;
    }
    [[nodiscard]] u32 bytes_per_pixel() const noexcept {
        switch (format) {
            case SurfaceFormat::R8:      return 1;
            case SurfaceFormat::RG8:     return 2;
            case SurfaceFormat::RGBA8:   return 4;
            case SurfaceFormat::R16F:    return 2;
            case SurfaceFormat::RGBA16F: return 8;
            case SurfaceFormat::R32F:    return 4;
            case SurfaceFormat::Depth24: return 4;
            case SurfaceFormat::Depth32F:return 4;
        }
        return 4;
    }
    [[nodiscard]] u64 estimated_bytes() const noexcept {
        return static_cast<u64>(width) * height * depth * layers
             * bytes_per_pixel() * sampleCount;
    }
};

struct SamplerDesc {
    enum class Filter : u8 { Nearest = 0, Linear };
    enum class Mipmap : u8 { Nearest = 0, Linear };
    enum class Wrap : u8 { Repeat = 0, ClampToEdge, MirroredRepeat, ClampToBorder };
    enum class Compare : u8 { None = 0, LessEqual, GreaterEqual };

    Filter  minFilter = Filter::Linear;
    Filter  magFilter = Filter::Linear;
    Mipmap  mipmap    = Mipmap::Linear;
    Wrap    wrapU = Wrap::ClampToEdge;
    Wrap    wrapV = Wrap::ClampToEdge;
    Wrap    wrapW = Wrap::ClampToEdge;
    Compare compare = Compare::None;
    f32     maxAnisotropy = 1.0f;
    const char* debugName = nullptr;
};

// -----------------------------------------------------------------------------
// Shader e pipeline
// -----------------------------------------------------------------------------
enum class ShaderStage : u8 { Vertex = 0, Fragment, Compute };

struct ShaderDesc {
    ShaderStage stage = ShaderStage::Fragment;
    /// Fonte única. O backend traduz: SPIR-V direto no Vulkan, compilado para
    /// MSL no Metal (via SPIRV-Cross, no build, não em runtime).
    const char* source = nullptr;
    const char* entryPoint = "main";
    const char* debugName = nullptr;
};

/// Estado de pipeline. Só os campos que o compositor usa: sem estado de
/// rasterização exótico, porque o Aurea não desenha geometria arbitrária —
/// desenha quads e meshes.
///
/// Compute é um caso SEPARADO, não um pipeline gráfico com os estágios vazios.
/// Um pipeline de compute não tem anexos de cor, não tem rasterizador e não tem
/// blend de hardware; tratá-lo como gráfico com campos em zero produz um estado
/// inválido que o driver recusa no primeiro dispatch. Por isso `isCompute`
/// seleciona qual conjunto de campos vale.
struct PipelineDesc {
    bool isCompute = false;

    ShaderHandle vertexShader{};      ///< ignorado quando isCompute
    ShaderHandle fragmentShader{};    ///< ignorado quando isCompute
    ShaderHandle computeShader{};     ///< usado só quando isCompute

    BlendMode blend = BlendMode::Normal;
    bool depthTest = false;
    bool depthWrite = false;
    bool cullBackFace = false;
    bool wireframe = false;

    u32 colorAttachmentCount = 1;
    SurfaceFormat colorFormats[4] = {SurfaceFormat::RGBA16F};
    SurfaceFormat depthFormat = SurfaceFormat::Depth24;
    u32 sampleCount = 1;

    /// Deslocamento de profundidade para evitar z-fighting em decalques
    /// (ícones de gizmo, textura de chão). Sem isto, o gizmo pisca.
    bool  depthBiasEnabled = false;
    f32   depthBiasConstant = 0.0f;
    f32   depthBiasSlope = 0.0f;

    /// Montagem de vértices: o compositor usa triângulo; partículas usam
    /// pontos com tamanho variável. Declarado, não inferido.
    enum class Topology : u8 { TriangleList = 0, TriangleStrip, PointList, LineList } topology = Topology::TriangleList;
};

/// Descritor de recurso vinculado a um pipeline. Indexado por set/binding como
/// em Vulkan/Metal, mas sem expor nenhum dos dois.
struct ResourceBinding {
    enum class Kind : u8 { UniformBuffer, StorageBuffer, CombinedImageSampler, StorageImage, Texture } kind =
        Kind::CombinedImageSampler;
    u32 set = 0;
    u32 binding = 0;
    ShaderStage stages = ShaderStage::Fragment;
};

// -----------------------------------------------------------------------------
// Comando de desenho. Gravado numa lista, executado pelo backend.
// -----------------------------------------------------------------------------
struct DrawCall {
    PipelineHandle pipeline{};
    /// Vértices diretos (o caso do compositor: um quad de 4 vértices).
    u32 vertexCount = 0;
    u32 vertexOffset = 0;
    BufferHandle vertexBuffer{};
    BufferHandle indexBuffer{};
    u32 indexCount = 0;
    u32 firstIndex = 0;
    i32 vertexBase = 0;

    /// Draw indireto: o número de instâncias/draws é decidido na GPU. É o
    /// caminho do culling por GPU e das partículas — sem isto, 100 mil
    /// partículas viram 100 mil chamadas de desenho e o driver engasga.
    bool indirect = false;
    BufferHandle indirectBuffer{};
    u64  indirectOffset = 0;
    u32  instanceCount = 1;
    u32  firstInstance = 0;
    u32  drawCount = 1;    ///< para multidraw indireto
};

/// Lista de comandos. O backend grava; a engine só preenche os dados.
class CommandList {
public:
    virtual ~CommandList() = default;

    [[nodiscard]] virtual Status begin() noexcept = 0;
    [[nodiscard]] virtual Status end() noexcept = 0;

    virtual void bind_pipeline(PipelineHandle p) noexcept = 0;
    virtual void bind_texture(u32 set, u32 binding, TextureHandle t, SamplerHandle s) noexcept = 0;
    virtual void bind_uniform(u32 set, u32 binding, const void* data, u32 bytes) noexcept = 0;
    virtual void bind_storage_buffer(u32 set, u32 binding, BufferHandle b) noexcept = 0;

    virtual void set_viewport(f32 x, f32 y, f32 w, f32 h) noexcept = 0;
    virtual void set_scissor(i32 x, i32 y, u32 w, u32 h) noexcept = 0;

    /// Assume a barreira necessária entre o render target anterior e este.
    /// O chamador não gerencia layout de imagem — é o tipo de coisa que erra
    /// silenciosamente e produz artefato só em alguns aparelhos.
    virtual void begin_render_pass(RenderPassHandle pass, TextureHandle color,
                                   TextureHandle depth, TextureHandle resolve,
                                   const f32 clearColor[4]) noexcept = 0;
    virtual void end_render_pass() noexcept = 0;

    virtual void draw(const DrawCall& call) noexcept = 0;

    /// Compute. Partículas, redução de histograma, culling por GPU.
    virtual void dispatch(u32 groupX, u32 groupY, u32 groupZ) noexcept = 0;

    virtual void memory_barrier() noexcept = 0;

    /// Copia de buffer para textura (upload de textura, leitura de resultado).
    virtual void copy_buffer_to_texture(BufferHandle src, TextureHandle dst) noexcept = 0;
    virtual void copy_texture_to_buffer(TextureHandle src, BufferHandle dst) noexcept = 0;

    /// Marca de tempo para a telemetria. Sem timestamp query o painel mostra
    /// "GPU: ?" em vez de inventar um número.
    virtual void write_timestamp(QueryPoolHandle pool, u32 index) noexcept = 0;
};

// -----------------------------------------------------------------------------
// Backend
// -----------------------------------------------------------------------------
/// Superfície de apresentação. Android: ANativeWindow + VkSurfaceKHR.
/// iOS: CAMetalLayer.
struct SurfaceDesc {
    void*  nativeWindow = nullptr;
    u32    width  = 0;
    u32    height = 0;
    /// SDR por padrão. HDR só é pedido quando o composition é HDR E o display
    /// anuncia suporte — pedir HDR sem suporte produz imagem lavada.
    bool   hdrRequested = false;
    ColorSpace preferredColorSpace = ColorSpace::SRGB;
    u32    minImageCount = 2;
    /// VSync. O preview SEMPRE usa, senão o editor queima bateria desenhando
    /// frames que ninguém vê.
    bool   vsync = true;
};

class GPUBackend {
public:
    virtual ~GPUBackend() = default;

    [[nodiscard]] virtual const char* name() const noexcept = 0;

    // --- Ciclo de vida --------------------------------------------------------
    [[nodiscard]] virtual Status initialize() noexcept = 0;
    virtual void shutdown() noexcept = 0;

    /// Chamado quando o app volta do background. No Android o dispositivo pode
    /// ter sido perdido; no iOS a CAMetalLayer é recriada pelo sistema.
    [[nodiscard]] virtual Status recreate_surface(const SurfaceDesc& desc) noexcept = 0;
    [[nodiscard]] virtual Status resize_surface(u32 width, u32 height) noexcept = 0;

    /// Espera o frame anterior terminar e devolve a imagem de apresentação.
    [[nodiscard]] virtual Status begin_frame(TextureHandle& outBackbuffer) noexcept = 0;
    [[nodiscard]] virtual Status end_frame(CommandList& cmds) noexcept = 0;

    // --- Recursos -------------------------------------------------------------
    [[nodiscard]] virtual Result<TextureHandle>  create_texture(const TextureDesc&) noexcept = 0;
    [[nodiscard]] virtual Result<BufferHandle>   create_buffer(usize bytes, u32 usageFlags) noexcept = 0;
    [[nodiscard]] virtual Result<SamplerHandle>  create_sampler(const SamplerDesc&) noexcept = 0;
    [[nodiscard]] virtual Result<ShaderHandle>   create_shader(const ShaderDesc&) noexcept = 0;
    [[nodiscard]] virtual Result<PipelineHandle> create_pipeline(const PipelineDesc&) noexcept = 0;
    [[nodiscard]] virtual Result<RenderPassHandle> create_render_pass(const TextureDesc* colors, u32 colorCount,
                                                                     const TextureDesc* depth) noexcept = 0;
    [[nodiscard]] virtual Result<QueryPoolHandle> create_query_pool(u32 count) noexcept = 0;

    virtual void destroy_texture(TextureHandle) noexcept = 0;
    virtual void destroy_buffer(BufferHandle) noexcept = 0;
    virtual void destroy_sampler(SamplerHandle) noexcept = 0;
    virtual void destroy_shader(ShaderHandle) noexcept = 0;
    virtual void destroy_pipeline(PipelineHandle) noexcept = 0;
    virtual void destroy_render_pass(RenderPassHandle) noexcept = 0;
    virtual void destroy_query_pool(QueryPoolHandle) noexcept = 0;

    // --- Upload ---------------------------------------------------------------
    /// Sobe dados de CPU para uma textura. Usado em assets estáticos (fonte de
    /// glyph, LUT, textura importada). NÃO é o caminho de frame de vídeo.
    [[nodiscard]] virtual Status upload_texture(TextureHandle dst, const void* data,
                                                u32 bytesPerRow, u32 mipLevel) noexcept = 0;
    [[nodiscard]] virtual Status upload_buffer(BufferHandle dst, const void* data,
                                               usize bytes, usize offset) noexcept = 0;

    /// Mapeia um buffer para leitura. Só o export usa (ler o frame codificado
    /// de volta quando o encoder não aceita superfície direto).
    [[nodiscard]] virtual Status map_buffer(BufferHandle src, void*& outPtr) noexcept = 0;
    virtual void unmap_buffer(BufferHandle src) noexcept = 0;

    // --- Zero-copy de mídia ---------------------------------------------------
    //
    //  O ponto crítico do pipeline. Um frame decodificado pelo hardware chega
    //  como AHardwareBuffer (Android) ou CVPixelBuffer (iOS). Isto o importa
    //  como textura amostrável SEM cópia de CPU — e é por isso que o Aurea
    //  consegue compor 4K em tempo real onde um pipeline bitmap-based não
    //  consegue nem a 1/4 da resolução.
    //
    //  A sincronização é responsabilidade do backend: o decoder sinaliza um
    //  fence, o backend o espera antes de amostrar. Devolver a textura antes
    //  do fence é o bug clássico de "frame rasgado" — por isso a interface só
    //  expõe `import_external_image`, que já cuida disso.
    [[nodiscard]] virtual Result<TextureHandle> import_external_image(const ExternalImageHandle& img) noexcept = 0;
    virtual void release_external_image(TextureHandle imported) noexcept = 0;

    // --- Leitura --------------------------------------------------------------
    /// Lê um render target de volta para a CPU. Usado em testes de render
    /// (comparar frame produzido com frame esperado) e em export quando o
    /// encoder exige memória. É caro: força sincronização com a GPU.
    [[nodiscard]] virtual Status read_texture(TextureHandle src, void* outData,
                                              u32 bytesPerRow) noexcept = 0;

    // --- Sincronização --------------------------------------------------------
    virtual void wait_idle() noexcept = 0;

    /// Resultado das timestamp queries. Devolve false quando a plataforma não
    /// suporta — a telemetria mostra "não medido" em vez de zero.
    [[nodiscard]] virtual bool read_timestamps(QueryPoolHandle pool, u32 first, u32 count,
                                               f32* outMilliseconds) noexcept = 0;

    // --- Consultas ------------------------------------------------------------
    [[nodiscard]] virtual bool is_device_lost() const noexcept = 0;
    [[nodiscard]] virtual u32  current_frame_index() const noexcept = 0;
    [[nodiscard]] virtual u64  allocated_bytes() const noexcept = 0;

    /// Cria o backend adequado a esta plataforma: Vulkan no Android, Metal no
    /// iOS. Devolve nullptr se nenhum estiver disponível — e nesse caso o app
    /// informa o usuário em vez de tentar um caminho degradado escondido.
    [[nodiscard]] static GPUBackend* create_default() noexcept;
};

} // namespace aurea

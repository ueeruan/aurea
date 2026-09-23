// =============================================================================
//  Aurea / gpu / metal / MetalInternal.hpp
//
//  As peças internas do backend Metal. Só os `.mm` incluem este arquivo: o
//  contrato público (o que a camada de plataforma e a SESSÃO A2 veem) é
//  `MetalBackend.hpp`, que compila em qualquer host.
//
//  Peças (uma responsabilidade cada):
//
//    HandlePool     índice + geração empacotados no u64 do motor (mesmo desenho
//                   do Vulkan: um handle velho nunca resolve para o objeto novo
//                   do slot).
//    HostRing       anel linear de `MTLBuffer` com memória compartilhada
//                   (unified): uniforms e staging do frame, sem alocação por
//                   draw. Cresce com um bloco extra no frame em que não coube.
//    FrameContext   um por frame em voo: command buffer, valor do evento que o
//                   fence espera, anéis, buffer de timestamps e fila de
//                   destruição adiada.
//    CommandListImpl  tradução de `CommandList`: escolhe o encoder
//                   (render/compute/blit), mantém o espelho das amarrações e o
//                   aplica no draw/dispatch. NÃO existe pool de descritores nem
//                   conjunto de descritores — o índice Metal é o slot do motor.
//
//  Nada aqui é visível acima de `GPUBackend.hpp`: o FrameGraph, os efeitos e o
//  export continuam falando `TextureHandle`, `PipelineHandle` e `CommandList`.
// =============================================================================
#pragma once

#if !defined(__APPLE__)
    #error "MetalInternal.hpp e ObjC++ do backend Metal: so compila em Apple (iOS/macOS)"
#endif

#include "MetalBackend.hpp"

#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <atomic>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace aurea::mtl {

// =============================================================================
// Conversões e utilidades (MetalCommon.mm)
// =============================================================================
[[nodiscard]] Status check_ns(NSError* err, const char* what) noexcept;
[[nodiscard]] MTLPixelFormat to_mtl(SurfaceFormat f) noexcept;
[[nodiscard]] SurfaceFormat from_mtl(MTLPixelFormat f) noexcept;
[[nodiscard]] MTLVertexFormat to_mtl(VertexFormat f) noexcept;
[[nodiscard]] MTLCompareFunction to_mtl(CompareOp c) noexcept;
[[nodiscard]] MTLPrimitiveType to_mtl(Topology t) noexcept;
[[nodiscard]] MTLPrimitiveTopologyClass to_mtl_topology(Topology t) noexcept;
[[nodiscard]] MTLSamplerAddressMode to_mtl(SamplerDesc::Wrap w) noexcept;
[[nodiscard]] MTLIndexType to_mtl(IndexType t) noexcept;
[[nodiscard]] bool is_compressed_format(MTLPixelFormat f) noexcept;
/// "vs_main" / "fs_main" / "cs_main" — a convenção do blob MSL (msl_glue.md).
[[nodiscard]] const char* stage_entry_point(ShaderStage stage) noexcept;
/// `label` no objeto Metal (aparece no capturador do Xcode). Sem custo em release
/// fora do `@autoreleasepool` de quem chama.
void set_object_label(id object, const char* name) noexcept;

// =============================================================================
// Pool de handles: índice + geração empacotados no u64 do motor.
// =============================================================================
template <typename T>
class HandlePool {
public:
    u64 add(T&& value) {
        u32 index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            index = static_cast<u32>(slots_.size());
            slots_.emplace_back();
        }
        Slot& s = slots_[index];
        s.value = std::move(value);
        s.alive = true;
        return (static_cast<u64>(s.generation) << 32) | (index + 1u);
    }
    T* get(u64 id) noexcept {
        const u32 index = static_cast<u32>(id & 0xFFFFFFFFu);
        if (index == 0 || index > slots_.size()) return nullptr;
        Slot& s = slots_[index - 1];
        if (!s.alive || s.generation != static_cast<u32>(id >> 32)) return nullptr;
        return &s.value;
    }
    const T* get(u64 id) const noexcept { return const_cast<HandlePool*>(this)->get(id); }
    bool remove(u64 id, T& out) {
        T* v = get(id);
        if (!v) return false;
        const u32 index = static_cast<u32>(id & 0xFFFFFFFFu) - 1u;
        out = std::move(*v);
        slots_[index].alive = false;
        ++slots_[index].generation;
        free_.push_back(index);
        return true;
    }
    template <typename Fn>
    void for_each(Fn&& fn) {
        for (u32 i = 0; i < slots_.size(); ++i) {
            if (slots_[i].alive) fn((static_cast<u64>(slots_[i].generation) << 32) | (i + 1u), slots_[i].value);
        }
    }
    [[nodiscard]] u32 count() const noexcept { return static_cast<u32>(slots_.size() - free_.size()); }
    void clear() { slots_.clear(); free_.clear(); }

private:
    struct Slot { T value{}; u32 generation = 1; bool alive = false; };
    std::vector<Slot> slots_;
    std::vector<u32> free_;
};

// =============================================================================
// Recursos
// =============================================================================
struct Texture {
    id<MTLTexture> texture = nil;
    TextureDesc    desc{};
    MTLPixelFormat format = MTLPixelFormatInvalid;
    ResourceState  state = ResourceState::Undefined;
    bool           ownsTexture = true;      ///< false: textura do drawable da layer
    // Importação externa (CVPixelBuffer do VideoToolbox). Os refs de CoreVideo
    // são CF: não são gerenciados por ARC e são liberados em destroy_texture_now.
    bool              external = false;
    bool              externalRgb = true;   ///< a amostra já sai em RGB
    u64               colorKey = 0;
    CVMetalTextureRef cvTexture = nullptr;  ///< mantém a IOSurface mapeada enquanto a textura vive
    CVPixelBufferRef  pixelBuffer = nullptr;///< retido: o buffer não pode voltar ao decoder em uso
    u64               lastUsedFrame = 0;
};

struct Buffer {
    id<MTLBuffer> buffer = nil;
    BufferDesc    desc{};
    bool          managed = false;   ///< MTLStorageModeManaged (macOS, leitura de volta)
    bool          hostVisible = false;
};

struct ShaderObject {
    id<MTLLibrary>  library = nil;
    id<MTLFunction> function = nil;
    ShaderStage     stage = ShaderStage::Fragment;
    /// Tamanho do grupo de threads (só compute): o Metal não o conhece pelo
    /// pipeline, então vem declarado no bloco MSL (msl_glue.md).
    u32 threadgroup[3] = {0, 0, 0};
};

struct PipelineObject {
    id<MTLRenderPipelineState>  render = nil;
    id<MTLComputePipelineState> compute = nil;
    id<MTLDepthStencilState>    depth = nil;
    /// Só compute: o tamanho do grupo de threads, que o Metal não guarda no
    /// pipeline — vem declarado no shader e é passado no `dispatchThreadgroups:`.
    u32  threadgroup[3] = {1, 1, 1};
    bool isCompute = false;
    /// Topologia do draw (o `drawPrimitives:` do Metal a pede por chamada).
    MTLPrimitiveType topology = MTLPrimitiveTypeTriangle;
    // Estado que no Metal mora no ENCODER, não no pipeline: aplicado no draw.
    MTLCullMode cull = MTLCullModeNone;
    MTLWinding  winding = MTLWindingCounterClockwise;
    bool        depthBiasEnabled = false;
    f32         depthBias = 0.0f;
    f32         depthBiasSlope = 0.0f;
};

struct SamplerObject {
    id<MTLSamplerState> sampler = nil;
};

// =============================================================================
// Anel linear de memória compartilhada (uniforms, staging).
// =============================================================================
class HostRing {
public:
    [[nodiscard]] bool initialize(id<MTLDevice> device, usize capacity, const char* name) noexcept;
    void shutdown() noexcept;
    void reset() noexcept;
    /// Reserva `size` bytes alinhados. Devolve buffer, deslocamento e ponteiro.
    [[nodiscard]] bool allocate(usize size, usize align, id<MTLBuffer>& outBuffer, u32& outOffset,
                                void*& outPtr) noexcept;
    [[nodiscard]] usize used() const noexcept { return used_; }

private:
    struct Chunk { id<MTLBuffer> buffer = nil; usize size = 0; };
    [[nodiscard]] bool add_chunk(usize size) noexcept;
    id<MTLDevice> device_ = nil;
    std::vector<Chunk> chunks_;
    usize offset_ = 0;
    usize used_ = 0;
    usize peak_ = 0;
    const char* name_ = "";
};

// =============================================================================
// Contexto de frame
// =============================================================================
struct DeferredRelease {
    void (*fn)(void*) = nullptr;
    void* ctx = nullptr;
};

struct FrameContext {
    id<MTLCommandBuffer>    cmd = nil;
    /// Valor que este frame sinaliza no `MTLSharedEvent` compartilhado do
    /// backend. É o fence por frame: `waitUntilSignaledValue:timeoutMS:` espera
    /// o frame N sem parar a fila inteira (o `VkFence` por frame do Vulkan).
    u64                     signalValue = 0;
    u64                     frameNumber = 0;
    bool                    submitted = false;
    /// Se ESTE frame já usou o fence do backend. Quem decide se a GPU terminou é
    /// `MTLSharedEvent.signaledValue` (o `VkFence` do Vulkan) — não há handler de
    /// conclusão mexendo em estado: a fila adiada roda na thread de render, no
    /// begin_frame que recicla o contexto, a mesma semântica de thread do Vulkan.
    bool                    waitsOnEvent = false;

    HostRing                uniforms;
    HostRing                staging;

    id<MTLCounterSampleBuffer> counterBuffer = nil;
    u32                     counterSamples = 0;
    std::vector<const char*> timerLabels;   ///< pares de amostras (início, fim)
    std::vector<u32>        timerStack;
    bool                    timersWritten = false;

    std::vector<DeferredRelease> deferred;
};

// =============================================================================
// Lista de comandos
//
// O encoder é escolhido pelo pipeline amarrado: gráfico → encoder de render
// (criado no begin_render_pass), compute → encoder de compute (criado no
// bind_pipeline). Cópia e leitura de volta passam pelo encoder de blit. Trocar
// de encoder encerra o anterior e reaplica o espelho das amarrações — em Metal
// um encoder por vez, sempre.
// =============================================================================
class CommandListImpl final : public CommandList {
public:
    void bind_frame(Backend* backend, FrameContext* frame) noexcept;

    void barrier(TextureHandle texture, ResourceState newState, bool discard) noexcept override;
    void begin_render_pass(const RenderPassBegin& pass) noexcept override;
    void end_render_pass() noexcept override;
    void bind_pipeline(PipelineHandle pipeline) noexcept override;
    void bind_texture(u32 slot, TextureHandle texture, SamplerHandle sampler) noexcept override;
    void bind_storage_image(u32 slot, TextureHandle texture) noexcept override;
    void bind_storage_buffer(BufferHandle buffer) noexcept override;
    void bind_storage_buffer_at(u32 slot, BufferHandle buffer) noexcept override;
    void set_uniforms(const void* data, u32 bytes) noexcept override;
    void push_constants(const void* data, u32 bytes) noexcept override;
    void set_viewport(f32 x, f32 y, f32 w, f32 h) noexcept override;
    void set_scissor(i32 x, i32 y, u32 w, u32 h) noexcept override;
    void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex) noexcept override;
    void bind_vertex_buffer(u32 binding, BufferHandle buffer, u64 offset) noexcept override;
    void bind_index_buffer(BufferHandle buffer, u64 offset, IndexType type) noexcept override;
    void draw_indexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset,
                      u32 firstInstance) noexcept override;
    void dispatch(u32 x, u32 y, u32 z) noexcept override;
    void copy_texture(TextureHandle src, TextureHandle dst) noexcept override;
    void copy_texture_to_buffer(TextureHandle src, BufferHandle dst) noexcept override;
    void begin_timer(const char* label) noexcept override;
    void end_timer() noexcept override;
    void begin_label(const char* label) noexcept override;
    void end_label() noexcept override;

    [[nodiscard]] bool in_render_pass() const noexcept { return enc_ == Enc::Render; }
    /// Encerra o encoder aberto. Chamado pelo `end_frame` e antes de qualquer
    /// troca de encoder — em Metal não existe "dois encoders ativos".
    void finish_encoders() noexcept;

private:
    enum class Enc { None, Render, Compute, Blit };

    [[nodiscard]] id<MTLCommandBuffer> cmd_buffer() const noexcept { return cmd_; }
    void end_current() noexcept;
    [[nodiscard]] id<MTLBlitCommandEncoder> blit_encoder() noexcept;
    [[nodiscard]] id<MTLComputeCommandEncoder> compute_encoder() noexcept;
    void apply_pipeline_state() noexcept;
    void apply_bindings() noexcept;
    [[nodiscard]] bool prepare_draw() noexcept;
    [[nodiscard]] id<MTLTexture> resolve_texture(u64 id) const noexcept;
    [[nodiscard]] id<MTLSamplerState> resolve_sampler(u64 id) const noexcept;

    Backend* backend_ = nullptr;
    Impl* impl_ = nullptr;
    FrameContext* frame_ = nullptr;
    id<MTLCommandBuffer> cmd_ = nil;

    Enc enc_ = Enc::None;
    id<MTLRenderCommandEncoder>  render_ = nil;
    id<MTLComputeCommandEncoder> compute_ = nil;
    id<MTLBlitCommandEncoder>    blit_ = nil;

    const PipelineObject* pipeline_ = nullptr;
    bool pipelineDirty_ = false;

    struct TexBinding { u64 texture = 0; u64 sampler = 0; };
    TexBinding textures_[binding::kTextureSlots]{};
    u64 storageImages_[binding::kStorageImageSlots]{};
    u64 storageBuffers_[binding::kStorageBufferSlots]{};
    u16 texturesDirty_ = 0;    ///< bit por slot de textura
    u32 dirty_ = 0;            ///< ver DirtyFlag
    bool storageDirty_ = false;

    id<MTLBuffer> uniformBuffer_ = nil;
    u32 uniformOffset_ = 0;
    u32 uniformSize_ = 0;
    u8  pushBytes_[binding::kPushConstantBytes]{};
    u32 pushSize_ = 0;
    bool pushDirty_ = false;

    struct VertexBinding { u64 buffer = 0; u64 offset = 0; };
    VertexBinding vertices_[VertexLayout::kMaxBindings]{};
    u64 indexBuffer_ = 0;
    u64 indexOffset_ = 0;
    MTLIndexType indexType_ = MTLIndexTypeUInt16;

    f32 viewport_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    i32 scissor_[4] = {0, 0, 0, 0};

    enum DirtyFlag : u32 {
        kDirtyUniform  = 1u << 0,
        kDirtyStorage  = 1u << 1,
        kDirtyPush     = 1u << 2,
        kDirtyVertex   = 1u << 3,
        kDirtyIndex    = 1u << 4,
        kDirtyViewport = 1u << 5,
        kDirtyScissor  = 1u << 6,
        kDirtyAll      = 0xFFFFFFFFu,
    };
    u32 labelDepth_ = 0;
};

// =============================================================================
// O backend por dentro
// =============================================================================
struct Impl {
    Backend* self = nullptr;
    BackendConfig config{};
    GPUCapabilities caps{};
    bool initialized = false;
    bool deviceLost = false;
    bool timersEnabled = false;
    void* requestedDevice = nullptr;   ///< `set_device` antes de `initialize`

    id<MTLDevice>       device = nil;
    id<MTLCommandQueue> queue = nil;
    /// Fence por frame (o `VkFence` do Vulkan). `waitUntilSignaledValue:` aceita
    /// tempo limite e não consome nada: `wait_frame` é seguro de qualquer thread.
    id<MTLSharedEvent>  event = nil;
    u64                 eventValue = 0;

    CVMetalTextureCacheRef textureCache = nullptr;

    /// Capacidade de amostragem de contadores: descoberta uma vez, na abertura.
    bool counterSampling = false;
    MTLCounterSamplingPoint counterPoint = MTLCounterSamplingPointAtBlitBoundary;
    id<MTLCounterSet> timestampSet = nil;
    /// Quantos passes cabem no buffer de amostras que o device aceitou. O Metal
    /// não publica um teto de `sampleCount`, então o backend PEDE o teto do
    /// motor e, se o device recusar, pede de novo menor — e este número é o que
    /// de fato existe (o painel DEV não inventa medida).
    u32 maxTimerSlots = 0;

    HandlePool<Texture>        textures;
    HandlePool<Buffer>         buffers;
    HandlePool<ShaderObject>   shaders;
    HandlePool<PipelineObject> pipelines;
    HandlePool<SamplerObject>  samplers;

    u64 dummyTexture = 0;
    u64 dummyStorage = 0;
    u64 dummyBuffer = 0;
    id<MTLSamplerState> defaultSampler = nil;
    /// Estado de profundidade por (teste, escrita, comparação, formato).
    std::unordered_map<u64, id<MTLDepthStencilState>> depthStates;

    std::unordered_map<void*, u64> importedByBuffer;   ///< CVPixelBufferRef → textura

    // Frames
    u32 framesInFlight = 2;
    FrameContext frames[3];
    u32 frameCursor = 0;
    u64 frameNumber = 0;
    FrameContext* current = nullptr;
    FrameContext* lastSubmitted = nullptr;
    CommandListImpl commands;

    // Superfície (CAMetalLayer)
    id<CAMetalLayer>    layer = nil;
    id<CAMetalDrawable> drawable = nil;
    SurfaceDesc         surfaceDesc{};
    u64                 drawableHandles[3]{};   ///< handles das texturas de drawable
    u32                 drawableCursor = 0;
    bool                drawableAcquired = false;   ///< o frame atual tem drawable?

    // Cache de pipeline (MTLBinaryArchive)
    id<MTLBinaryArchive> archive = nil;
    std::string cachePath;
    u64 lastSavedCacheBytes = 0;
    /// `GPUBackend::PipelineCacheInfo` (o tipo é aninhado na interface: fora
    /// dela, o nome precisa do dono).
    GPUBackend::PipelineCacheInfo cacheInfo{};

    // Medição
    std::vector<GpuTiming> timings;
    f32 timingTotalMs = 0.0f;
    bool timingsValid = false;
    u64 uploadBytesFrame = 0;

    // Contabilidade de memória (para o painel)
    u32 pipelinesSinceSave = 0;   ///< pipeline criado desde a última gravação do cache
    u64 textureBytes = 0;
    u64 bufferBytes = 0;
    u32 allocationCount = 0;

    [[nodiscard]] const PipelineObject* pipeline(u64 id) noexcept { return pipelines.get(id); }

    // --- Peças usadas por mais de um arquivo ---------------------------------
    [[nodiscard]] Status create_frames() noexcept;
    void destroy_frames() noexcept;
    [[nodiscard]] Status create_dummies() noexcept;
    [[nodiscard]] Status submit_immediate(void (*record)(Impl&, id<MTLCommandBuffer>, void*), void* ctx) noexcept;
    void run_deferred(FrameContext& f) noexcept;
    void collect_timings(FrameContext& f) noexcept;
    void wait_frame_gpu(FrameContext& f) noexcept;
    FrameContext* deferral_target() noexcept;
    [[nodiscard]] Status begin_frame_impl(FrameBegin& out, bool withSurface) noexcept;
    void destroy_texture_now(Texture& t) noexcept;
    void destroy_buffer_now(Buffer& b) noexcept;
    [[nodiscard]] id<MTLDepthStencilState> depth_state(const PipelineDesc& desc) noexcept;
    void fill_capabilities() noexcept;
    void load_pipeline_cache() noexcept;
    /// Marca de tempo: um encoder de blit só dela, com barreira (é o ponto de
    /// amostragem que mede ENTRE passes sem mexer no estado de nenhum passe).
    void sample_counter(u32 index) noexcept;
    /// Devolve à CPU (e ao decoder) as importações que ninguém usa há muito.
    void reclaim_stale_imports() noexcept;
    /// Registra/atualiza a textura do drawable como um recurso do pool.
    [[nodiscard]] TextureHandle register_drawable(id<MTLTexture> texture, u32 width, u32 height) noexcept;
    void release_drawable_textures() noexcept;
};

} // namespace aurea::mtl

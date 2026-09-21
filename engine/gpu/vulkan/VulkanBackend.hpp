// =============================================================================
//  Aurea / gpu / vulkan / VulkanBackend.hpp
//
//  O renderer real do Aurea no Android — e o mesmo código roda no host de
//  testes (Windows/Linux), sem superfície, para os testes visuais usarem os
//  MESMOS shaders SPIR-V e o MESMO caminho de comandos do aparelho.
//
//  Peças (uma responsabilidade cada):
//
//    MemoryAllocator   blocos por tipo de memória, subalocação com lista livre,
//                      alocação dedicada para o que é grande. Nada de
//                      vkAllocateMemory por textura.
//    FrameContext      um por frame em voo: command pool, command buffer,
//                      fence, semáforo de aquisição, pools de descritor, anel
//                      de uniforms, anel de staging, timestamp queries e fila
//                      de destruição adiada. CPU grava o N+1 enquanto a GPU
//                      executa o N.
//    Swapchain         ANativeWindow → VkSurface → swapchain FIFO, com
//                      pré-rotação e recriação sem tocar no resto.
//    Caches            pipeline (persistido em disco), render pass, framebuffer,
//                      layout de pipeline, conversão YCbCr, sampler.
//    CommandListImpl   tradução de `CommandList`: barreiras mínimas a partir do
//                      estado rastreado, descritores montados por draw a partir
//                      de slots, uniforms no anel do frame.
// =============================================================================
#pragma once

#include "VulkanLoader.hpp"
#include "aurea/render/GPUBackend.hpp"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
struct AHardwareBuffer;
#endif

namespace aurea::vk {

[[nodiscard]] Status check(VkResult r, const char* what) noexcept;
[[nodiscard]] const char* result_name(VkResult r) noexcept;
[[nodiscard]] VkFormat to_vk(SurfaceFormat f) noexcept;
[[nodiscard]] SurfaceFormat from_vk(VkFormat f) noexcept;

// -----------------------------------------------------------------------------
// Pool de handles: índice + geração empacotados no u64 do motor. Busca O(1),
// e um handle velho (destruído) nunca resolve para o objeto novo do slot.
// -----------------------------------------------------------------------------
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
    [[nodiscard]] u32 count() const noexcept {
        return static_cast<u32>(slots_.size() - free_.size());
    }
    void clear() { slots_.clear(); free_.clear(); }

private:
    struct Slot { T value{}; u32 generation = 1; bool alive = false; };
    std::vector<Slot> slots_;
    std::vector<u32> free_;
};

// -----------------------------------------------------------------------------
// Memória
// -----------------------------------------------------------------------------
struct Allocation {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize   offset = 0;
    VkDeviceSize   size = 0;
    u32            memoryType = 0;
    u32            block = kInvalidIndex;   ///< kInvalidIndex = dedicada
    void*          mapped = nullptr;
    [[nodiscard]] bool valid() const noexcept { return memory != VK_NULL_HANDLE; }
};

class MemoryAllocator {
public:
    void initialize(VkPhysicalDevice physical, VkDevice device) noexcept;
    void shutdown() noexcept;

    /// `required`: propriedades obrigatórias; `preferred`: desejáveis.
    [[nodiscard]] Allocation allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags required,
                                      VkMemoryPropertyFlags preferred, bool dedicated,
                                      const char* debugName) noexcept;
    void free(const Allocation& a) noexcept;

    [[nodiscard]] u64 reserved_bytes() const noexcept { return reserved_; }
    [[nodiscard]] u64 used_bytes() const noexcept { return used_; }
    [[nodiscard]] u32 block_count() const noexcept { return static_cast<u32>(blocks_.size()); }
    [[nodiscard]] u32 allocation_count() const noexcept { return allocations_; }
    [[nodiscard]] bool unified() const noexcept { return unified_; }
    [[nodiscard]] const VkPhysicalDeviceMemoryProperties& properties() const noexcept { return props_; }

private:
    struct Range { VkDeviceSize offset; VkDeviceSize size; };
    struct Block {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        u32 memoryType = 0;
        void* mapped = nullptr;
        std::vector<Range> free;   ///< ordenado por offset, coalescido
        VkDeviceSize used = 0;
    };
    [[nodiscard]] u32 find_type(u32 bits, VkMemoryPropertyFlags required,
                                VkMemoryPropertyFlags preferred) const noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties props_{};
    VkDeviceSize granularity_ = 1;
    std::vector<Block> blocks_;
    u64 reserved_ = 0;
    u64 used_ = 0;
    u32 allocations_ = 0;
    bool unified_ = false;
    std::mutex mutex_;
};

// -----------------------------------------------------------------------------
// Recursos
// -----------------------------------------------------------------------------
struct Texture {
    VkImage       image = VK_NULL_HANDLE;
    VkImageView   view = VK_NULL_HANDLE;
    Allocation    alloc{};
    TextureDesc   desc{};
    VkFormat      format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    ResourceState state = ResourceState::Undefined;
    bool          ownsImage = true;          ///< false: imagem do swapchain
    // Imagem externa (AHardwareBuffer do decoder)
    bool          external = false;
    bool          acquiredThisFrame = false;
    VkDeviceMemory importedMemory = VK_NULL_HANDLE;
    u64           ycbcrSampler = 0;
    void*         nativeBuffer = nullptr;
    u64           lastUsedFrame = 0;
    VkFramebuffer framebuffers[3]{};         ///< um por LoadOp
};

struct Buffer {
    VkBuffer   buffer = VK_NULL_HANDLE;
    Allocation alloc{};
    BufferDesc desc{};
};

struct ShaderModule {
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage stage = ShaderStage::Fragment;
};

struct PipelineObject {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
};

struct SamplerObject {
    VkSampler sampler = VK_NULL_HANDLE;
    VkSamplerYcbcrConversion conversion = VK_NULL_HANDLE;   ///< só em sampler de vídeo externo
    bool shared = false;   ///< do cache: não é destruído pelo handle
};

// -----------------------------------------------------------------------------
// Anel linear host-visible (uniforms, staging). Cresce com um bloco extra no
// frame em que não coube; no reset, os extras viram um bloco único maior.
// -----------------------------------------------------------------------------
class HostRing {
public:
    void initialize(class Backend* backend, VkDeviceSize capacity, VkBufferUsageFlags usage,
                    const char* name) noexcept;
    void shutdown() noexcept;
    void reset() noexcept;
    /// Reserva `size` bytes alinhados. Devolve buffer, offset e ponteiro.
    [[nodiscard]] bool allocate(VkDeviceSize size, VkDeviceSize align, VkBuffer& outBuffer,
                                VkDeviceSize& outOffset, void*& outPtr) noexcept;
    [[nodiscard]] VkDeviceSize used() const noexcept { return used_; }

private:
    struct Chunk { VkBuffer buffer = VK_NULL_HANDLE; Allocation alloc{}; VkDeviceSize size = 0; };
    [[nodiscard]] bool add_chunk(VkDeviceSize size) noexcept;
    class Backend* backend_ = nullptr;
    std::vector<Chunk> chunks_;
    VkDeviceSize offset_ = 0;
    VkDeviceSize used_ = 0;
    VkDeviceSize peak_ = 0;
    VkBufferUsageFlags usage_ = 0;
    const char* name_ = "";
};

class Backend;

// -----------------------------------------------------------------------------
// Lista de comandos
// -----------------------------------------------------------------------------
class CommandListImpl final : public CommandList {
public:
    void bind_frame(Backend* backend, struct FrameContext* frame, VkCommandBuffer cmd) noexcept;

    void barrier(TextureHandle texture, ResourceState newState, bool discard) noexcept override;
    void begin_render_pass(const RenderPassBegin& pass) noexcept override;
    void end_render_pass() noexcept override;
    void bind_pipeline(PipelineHandle pipeline) noexcept override;
    void bind_texture(u32 slot, TextureHandle texture, SamplerHandle sampler) noexcept override;
    void bind_storage_image(u32 slot, TextureHandle texture) noexcept override;
    void bind_storage_buffer(BufferHandle buffer) noexcept override;
    void set_uniforms(const void* data, u32 bytes) noexcept override;
    void push_constants(const void* data, u32 bytes) noexcept override;
    void set_viewport(f32 x, f32 y, f32 w, f32 h) noexcept override;
    void set_scissor(i32 x, i32 y, u32 w, u32 h) noexcept override;
    void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex) noexcept override;
    void dispatch(u32 x, u32 y, u32 z) noexcept override;
    void copy_texture(TextureHandle src, TextureHandle dst) noexcept override;
    void copy_texture_to_buffer(TextureHandle src, BufferHandle dst) noexcept override;
    void begin_timer(const char* label) noexcept override;
    void end_timer() noexcept override;
    void begin_label(const char* label) noexcept override;
    void end_label() noexcept override;

    [[nodiscard]] bool in_render_pass() const noexcept { return inRenderPass_; }
    [[nodiscard]] VkCommandBuffer handle() const noexcept { return cmd_; }

private:
    [[nodiscard]] bool flush_descriptors() noexcept;

    Backend* backend_ = nullptr;
    FrameContext* frame_ = nullptr;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    bool inRenderPass_ = false;
    u32 targetWidth_ = 0, targetHeight_ = 0;

    const PipelineObject* pipeline_ = nullptr;
    struct TexBinding { u64 texture = 0; u64 sampler = 0; };
    TexBinding textures_[binding::kTextureSlots]{};
    u64 storageImages_[binding::kStorageImageSlots]{};
    u64 storageBuffer_ = 0;
    VkBuffer uniformBuffer_ = VK_NULL_HANDLE;
    u32 uniformOffset_ = 0;
    u32 uniformSize_ = 0;
    VkDescriptorSet lastSet_ = VK_NULL_HANDLE;
    bool dirty_ = true;
};

// -----------------------------------------------------------------------------
// Contexto de frame
// -----------------------------------------------------------------------------
struct DeferredRelease {
    void (*fn)(void*) = nullptr;
    void* ctx = nullptr;
};

struct FrameContext {
    VkCommandPool   pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence         fence = VK_NULL_HANDLE;
    VkSemaphore     acquired = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> descriptorPools;
    u32             descriptorPoolCursor = 0;
    HostRing        uniforms;
    HostRing        staging;
    VkQueryPool     queries = VK_NULL_HANDLE;
    u32             queryCount = 0;
    std::vector<const char*> timerLabels;   ///< pares de queries (início, fim)
    std::vector<u32> timerStack;
    bool            timersWritten = false;
    std::vector<DeferredRelease> deferred;
    std::vector<u64> externalAcquired;       ///< texturas externas lidas neste frame
    u64             frameNumber = 0;
    bool            submitted = false;
};

// -----------------------------------------------------------------------------
// O backend
// -----------------------------------------------------------------------------
class Backend final : public GPUBackend {
public:
    Backend() = default;
    ~Backend() override;

    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    [[nodiscard]] const char* name() const noexcept override { return "Vulkan"; }
    [[nodiscard]] const GPUCapabilities& capabilities() const noexcept override { return caps_; }

    [[nodiscard]] Status initialize(const BackendConfig& config) noexcept override;
    void shutdown() noexcept override;

    [[nodiscard]] Status attach_surface(const SurfaceDesc& desc) noexcept override;
    void detach_surface() noexcept override;
    [[nodiscard]] Status resize_surface(u32 width, u32 height) noexcept override;
    [[nodiscard]] bool has_surface() const noexcept override { return swapchain_ != VK_NULL_HANDLE; }

    [[nodiscard]] Status begin_frame(FrameBegin& out) noexcept override;
    [[nodiscard]] Status end_frame() noexcept override;

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
    [[nodiscard]] Status read_texture(TextureHandle src, void* outData, u32 bytesPerRow) noexcept override;

    [[nodiscard]] Result<ExternalTexture> import_external_image(const ExternalImageDesc& img) noexcept override;
    void release_external_image(TextureHandle imported) noexcept override;
    void defer_until_gpu_done(void (*fn)(void*), void* ctx) noexcept override;

    void wait_idle() noexcept override;
    [[nodiscard]] u32 read_gpu_timings(GpuTiming* out, u32 capacity, f32* totalMs) noexcept override;
    [[nodiscard]] bool is_device_lost() const noexcept override { return deviceLost_; }
    [[nodiscard]] u32 frames_in_flight() const noexcept override { return framesInFlight_; }
    [[nodiscard]] GpuMemoryStats memory_stats() const noexcept override;
    void save_pipeline_cache() noexcept override;

    // --- Internos, usados pelas peças do backend -----------------------------
    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] MemoryAllocator& allocator() noexcept { return allocator_; }
    [[nodiscard]] Texture* texture(u64 id) noexcept { return textures_.get(id); }
    [[nodiscard]] Buffer* buffer(u64 id) noexcept { return buffers_.get(id); }
    [[nodiscard]] SamplerObject* sampler(u64 id) noexcept { return samplers_.get(id); }
    [[nodiscard]] const PipelineObject* pipeline(u64 id) noexcept { return pipelines_.get(id); }
    [[nodiscard]] VkRenderPass render_pass(VkFormat format, LoadOp load) noexcept;
    [[nodiscard]] VkFramebuffer framebuffer(Texture& t, LoadOp load) noexcept;
    [[nodiscard]] VkDescriptorSet allocate_set(FrameContext& frame, VkDescriptorSetLayout layout) noexcept;
    void transition(VkCommandBuffer cmd, Texture& t, ResourceState newState, bool discard) noexcept;
    [[nodiscard]] Texture& dummy_texture() noexcept { return *textures_.get(dummyTexture_); }
    [[nodiscard]] Texture& dummy_storage() noexcept { return *textures_.get(dummyStorage_); }
    [[nodiscard]] Buffer& dummy_buffer() noexcept { return *buffers_.get(dummyBuffer_); }
    [[nodiscard]] VkSampler default_sampler() const noexcept { return defaultSampler_; }
    [[nodiscard]] u32 uniform_alignment() const noexcept { return caps_.minUniformBufferOffsetAlignment; }
    [[nodiscard]] bool debug_utils() const noexcept { return debugUtils_; }
    [[nodiscard]] u32 graphics_family() const noexcept { return graphicsFamily_; }
    [[nodiscard]] u32 max_timers() const noexcept { return kMaxTimers; }
    [[nodiscard]] bool timers_enabled() const noexcept { return timersEnabled_; }
    void set_object_name(VkObjectType type, u64 handle, const char* name) noexcept;
    void destroy_texture_now(Texture& t) noexcept;
    void destroy_buffer_now(Buffer& b) noexcept;

    static constexpr u32 kMaxTimers = 64;

private:
    [[nodiscard]] Status create_instance(bool validation) noexcept;
    [[nodiscard]] Status pick_device() noexcept;
    [[nodiscard]] Status create_device() noexcept;
    void fill_capabilities() noexcept;
    [[nodiscard]] Status create_frames() noexcept;
    void destroy_frames() noexcept;
    [[nodiscard]] Status create_swapchain(u32 width, u32 height) noexcept;
    void destroy_swapchain() noexcept;
    [[nodiscard]] Status recreate_swapchain() noexcept;
    [[nodiscard]] Status create_dummies() noexcept;
    [[nodiscard]] VkPipelineLayout pipeline_layout(u64 immutableSampler, VkDescriptorSetLayout& outSet) noexcept;
    void run_deferred(FrameContext& f) noexcept;
    void collect_timings(FrameContext& f) noexcept;
    [[nodiscard]] Status submit_immediate(void (*record)(Backend&, VkCommandBuffer, void*), void* ctx) noexcept;
    [[nodiscard]] FrameContext* deferral_target() noexcept;
    void load_pipeline_cache() noexcept;
    [[nodiscard]] bool note_device_lost(VkResult r) noexcept;

    BackendConfig config_{};
    GPUCapabilities caps_{};
    bool initialized_ = false;
    bool deviceLost_ = false;
    bool debugUtils_ = false;
    bool timersEnabled_ = false;
    f32 timestampPeriod_ = 1.0f;
    u64 timestampMask_ = ~0ull;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    u32 graphicsFamily_ = 0;
    bool hasSurfaceExt_ = false;
    bool hasAhb_ = false;
    bool hasForeignQueue_ = false;
    bool hasYcbcr_ = false;
    u32 apiVersion_ = 0;

    MemoryAllocator allocator_;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    std::string cachePath_;

    HandlePool<Texture> textures_;
    HandlePool<Buffer> buffers_;
    HandlePool<ShaderModule> shaders_;
    HandlePool<PipelineObject> pipelines_;
    HandlePool<SamplerObject> samplers_;

    // Caches
    std::unordered_map<u64, VkRenderPass> renderPasses_;          ///< (formato, load)
    std::unordered_map<u64, VkPipelineLayout> layouts_;           ///< por sampler imutável
    std::unordered_map<u64, VkDescriptorSetLayout> setLayouts_;
    std::unordered_map<u64, u64> ycbcrByFormat_;                  ///< formato externo → sampler
    std::unordered_map<void*, u64> importedByBuffer_;             ///< AHardwareBuffer → textura

    u64 dummyTexture_ = 0;
    u64 dummyStorage_ = 0;
    u64 dummyBuffer_ = 0;
    VkSampler defaultSampler_ = VK_NULL_HANDLE;

    // Frames
    u32 framesInFlight_ = 2;
    FrameContext frames_[3];
    u32 frameCursor_ = 0;
    u64 frameNumber_ = 0;
    FrameContext* current_ = nullptr;
    FrameContext* lastSubmitted_ = nullptr;
    CommandListImpl commands_;

    // Superfície
    SurfaceDesc surfaceDesc_{};
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapExtent_{};
    VkSurfaceTransformFlagBitsKHR swapTransform_ = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    std::vector<u64> swapTextures_;
    std::vector<VkSemaphore> renderDone_;
    u32 imageIndex_ = 0;
    bool imageAcquired_ = false;
    bool swapchainDirty_ = false;

    // Medição
    std::vector<GpuTiming> timings_;
    f32 timingTotalMs_ = 0.0f;
    bool timingsValid_ = false;
    u64 uploadBytesFrame_ = 0;
};

} // namespace aurea::vk

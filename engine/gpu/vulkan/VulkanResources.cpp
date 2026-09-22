// =============================================================================
//  Recursos: texturas, buffers, samplers, shaders, pipelines, caches de render
//  pass / framebuffer / layout, upload, leitura de volta e importação zero-copy.
// =============================================================================
#include "VulkanBackend.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>
#include <cstring>

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    #include <android/hardware_buffer.h>
#endif

namespace aurea::vk {
namespace {

VkImageUsageFlags usage_for(const TextureDesc& d) noexcept {
    VkImageUsageFlags u = 0;
    if (d.sampled)      u |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (d.is_depth()) {
        // Profundidade: anexo, e amostrada quando é mapa de sombra.
        u |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (d.transferSrc) u |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        return u;
    }
    if (d.renderTarget) u |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    // Mips gerados por blit: o nível de origem é lido por transferência.
    if (d.mipLevels > 1) u |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (d.storage)      u |= VK_IMAGE_USAGE_STORAGE_BIT;
    // Leitura de volta de alvo de render (testes, export) e upload de textura
    // amostrada são comuns o bastante para valerem a flag desde a criação —
    // acrescentar depois exigiria recriar a imagem.
    if (d.transferSrc || d.renderTarget) u |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (d.transferDst || d.sampled)      u |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (u == 0) u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return u;
}

VkSamplerAddressMode to_vk(SamplerDesc::Wrap w) noexcept {
    switch (w) {
        case SamplerDesc::Wrap::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case SamplerDesc::Wrap::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case SamplerDesc::Wrap::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case SamplerDesc::Wrap::ClampToBorder:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

VkPrimitiveTopology to_vk(Topology t) noexcept {
    switch (t) {
        case Topology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case Topology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case Topology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case Topology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

u32 texel_bytes(VkFormat f) noexcept {
    switch (f) {
        case VK_FORMAT_R8_UNORM: return 1;
        case VK_FORMAT_R8G8_UNORM: return 2;
        case VK_FORMAT_R16_UNORM: case VK_FORMAT_R16_SFLOAT: return 2;
        case VK_FORMAT_R16G16_UNORM: case VK_FORMAT_R16G16_SFLOAT: return 4;
        case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_B8G8R8A8_UNORM: return 4;
        case VK_FORMAT_R32_SFLOAT: return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
        default: return 4;
    }
}

/// Destruição adiada: o objeto viaja num nó alocado até o fence do frame.
struct PendingTexture { Backend* backend; Texture texture; };
struct PendingBuffer { Backend* backend; Buffer buffer; };

} // namespace

void Backend::set_object_name(VkObjectType type, u64 handle, const char* name) noexcept {
    if (!debugUtils_ || !vkSetDebugUtilsObjectNameEXT || !name || !handle) return;
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    (void)vkSetDebugUtilsObjectNameEXT(device_, &info);
}

// =============================================================================
// Texturas
// =============================================================================
Result<TextureHandle> Backend::create_texture(const TextureDesc& desc) noexcept {
    if (!device_) return Status{Errc::InvalidState, "backend nao inicializado"};
    if (desc.width == 0 || desc.height == 0) return Status{Errc::InvalidArgument, "textura sem tamanho"};
    if (desc.width > caps_.maxTexture2D || desc.height > caps_.maxTexture2D) {
        return Status{Errc::OutOfRange, "textura maior que o limite do aparelho"};
    }

    Texture t;
    t.desc = desc;
    t.format = to_vk(desc.format);
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = t.format;
    info.extent = {desc.width, desc.height, 1};
    info.mipLevels = std::max(1u, desc.mipLevels);
    info.arrayLayers = std::max(1u, desc.layers);
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage_for(desc);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (desc.cube) {
        info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        info.arrayLayers = 6;
    }
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (const Status s = check(vkCreateImage(device_, &info, nullptr, &t.image), "vkCreateImage"); !s.ok()) return s;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, t.image, &req);
    t.alloc = allocator_.allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, false, desc.debugName);
    if (!t.alloc.valid()) {
        vkDestroyImage(device_, t.image, nullptr);
        return Status{Errc::OutOfDeviceMemory, "sem memoria para textura"};
    }
    if (const Status s = check(vkBindImageMemory(device_, t.image, t.alloc.memory, t.alloc.offset), "vkBindImageMemory");
        !s.ok()) {
        vkDestroyImage(device_, t.image, nullptr);
        allocator_.free(t.alloc);
        return s;
    }

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t.image;
    vi.viewType = desc.cube ? VK_IMAGE_VIEW_TYPE_CUBE
                : info.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    vi.format = t.format;
    vi.subresourceRange = {aspect_of(t.format), 0, info.mipLevels, 0, info.arrayLayers};
    if (const Status s = check(vkCreateImageView(device_, &vi, nullptr, &t.view), "vkCreateImageView"); !s.ok()) {
        vkDestroyImage(device_, t.image, nullptr);
        allocator_.free(t.alloc);
        return s;
    }
    set_object_name(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<u64>(t.image), desc.debugName);
    t.lastUsedFrame = frameNumber_;
    return TextureHandle{textures_.add(std::move(t))};
}

void Backend::destroy_texture_now(Texture& t) noexcept {
    for (VkFramebuffer& fb : t.framebuffers) {
        if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
        fb = VK_NULL_HANDLE;
    }
    for (Texture::DepthFramebuffer& d : t.depthFramebuffers) {
        if (d.fb) vkDestroyFramebuffer(device_, d.fb, nullptr);
    }
    t.depthFramebuffers.clear();
    if (t.view) vkDestroyImageView(device_, t.view, nullptr);
    if (t.ownsImage && t.image) vkDestroyImage(device_, t.image, nullptr);
    if (t.importedMemory) vkFreeMemory(device_, t.importedMemory, nullptr);
    allocator_.free(t.alloc);
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    if (t.nativeBuffer) AHardwareBuffer_release(static_cast<AHardwareBuffer*>(t.nativeBuffer));
#endif
    t = Texture{};
}

void Backend::destroy_texture(TextureHandle h) noexcept {
    Texture t;
    if (!textures_.remove(h.id, t)) return;
    if (is_depth_format(t.format)) {
        // Framebuffers de outras texturas que usam esta profundidade: saem
        // junto (a view vai ser destruída).
        textures_.for_each([&](u64, Texture& other) {
            for (auto it = other.depthFramebuffers.begin(); it != other.depthFramebuffers.end();) {
                if (it->depth != h.id) { ++it; continue; }
                struct Node { Backend* b; VkFramebuffer fb; };
                defer_until_gpu_done([](void* p) {
                    auto* n = static_cast<Node*>(p);
                    vkDestroyFramebuffer(n->b->device(), n->fb, nullptr);
                    delete n;
                }, new Node{this, it->fb});
                it = other.depthFramebuffers.erase(it);
            }
        });
    }
    if (t.external && t.nativeBuffer) importedByBuffer_.erase(t.nativeBuffer);
    auto* pending = new PendingTexture{this, std::move(t)};
    defer_until_gpu_done([](void* p) {
        auto* node = static_cast<PendingTexture*>(p);
        node->backend->destroy_texture_now(node->texture);
        delete node;
    }, pending);
}

TextureDesc Backend::texture_desc(TextureHandle h) const noexcept {
    const Texture* t = textures_.get(h.id);
    return t ? t->desc : TextureDesc{};
}

// =============================================================================
// Buffers
// =============================================================================
Result<BufferHandle> Backend::create_buffer(const BufferDesc& desc) noexcept {
    if (!device_) return Status{Errc::InvalidState, "backend nao inicializado"};
    if (desc.bytes == 0) return Status{Errc::InvalidArgument, "buffer vazio"};
    Buffer b;
    b.desc = desc;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = desc.bytes;
    VkBufferUsageFlags u = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (has_flag(desc.usage, BufferUsage::Uniform))  u |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (has_flag(desc.usage, BufferUsage::Storage))  u |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (has_flag(desc.usage, BufferUsage::Vertex))   u |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (has_flag(desc.usage, BufferUsage::Index))    u |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (has_flag(desc.usage, BufferUsage::Indirect)) u |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    info.usage = u;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (const Status s = check(vkCreateBuffer(device_, &info, nullptr, &b.buffer), "vkCreateBuffer"); !s.ok()) return s;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, b.buffer, &req);
    VkMemoryPropertyFlags required = 0, preferred = 0;
    switch (desc.access) {
        case MemoryAccess::GpuOnly:
            required = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case MemoryAccess::Upload:
            required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            preferred = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case MemoryAccess::Readback:
            required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            preferred = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            break;
    }
    b.alloc = allocator_.allocate(req, required, preferred, false, desc.debugName);
    if (!b.alloc.valid()
        || vkBindBufferMemory(device_, b.buffer, b.alloc.memory, b.alloc.offset) != VK_SUCCESS) {
        vkDestroyBuffer(device_, b.buffer, nullptr);
        allocator_.free(b.alloc);
        return Status{Errc::OutOfDeviceMemory, "sem memoria para buffer"};
    }
    set_object_name(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<u64>(b.buffer), desc.debugName);
    return BufferHandle{buffers_.add(std::move(b))};
}

void Backend::destroy_buffer_now(Buffer& b) noexcept {
    if (b.buffer) vkDestroyBuffer(device_, b.buffer, nullptr);
    allocator_.free(b.alloc);
    b = Buffer{};
}

void Backend::destroy_buffer(BufferHandle h) noexcept {
    Buffer b;
    if (!buffers_.remove(h.id, b)) return;
    auto* pending = new PendingBuffer{this, std::move(b)};
    defer_until_gpu_done([](void* p) {
        auto* node = static_cast<PendingBuffer*>(p);
        node->backend->destroy_buffer_now(node->buffer);
        delete node;
    }, pending);
}

Status Backend::write_buffer(BufferHandle dst, usize offset, const void* data, usize bytes) noexcept {
    Buffer* b = buffers_.get(dst.id);
    if (!b || !data) return Errc::InvalidArgument;
    if (offset + bytes > b->desc.bytes) return Errc::OutOfRange;
    if (b->alloc.mapped) {
        std::memcpy(static_cast<u8*>(b->alloc.mapped) + offset, data, bytes);
        return OkStatus;
    }
    // Buffer só de GPU: staging + cópia, síncrono (não é caminho de frame).
    // O VkBuffer de destino é copiado ANTES de criar o staging: `create_buffer`
    // pode realocar o pool e `b` ficaria pendurado (use-after-free achado pelo
    // ASan da Fase 8 no upload de modelo 3D, Gpu.Scene3D*).
    const VkBuffer dstBuffer = b->buffer;
    b = nullptr;
    BufferDesc sd;
    sd.bytes = bytes;
    sd.usage = BufferUsage::TransferSrc;
    sd.access = MemoryAccess::Upload;
    auto staging = create_buffer(sd);
    if (!staging.ok()) return staging.status();
    Buffer* s = buffers_.get(staging->id);
    std::memcpy(s->alloc.mapped, data, bytes);
    struct Ctx { VkBuffer src; VkBuffer dst; VkDeviceSize offset; VkDeviceSize size; }
        ctx{s->buffer, dstBuffer, offset, bytes};
    const Status st = submit_immediate([](Backend&, VkCommandBuffer cmd, void* p) {
        const Ctx* c = static_cast<const Ctx*>(p);
        VkBufferCopy region{0, c->offset, c->size};
        vkCmdCopyBuffer(cmd, c->src, c->dst, 1, &region);
    }, &ctx);
    Buffer dead;
    if (buffers_.remove(staging->id, dead)) destroy_buffer_now(dead);
    return st;
}

Status Backend::map_buffer(BufferHandle buffer, void*& outPtr) noexcept {
    Buffer* b = buffers_.get(buffer.id);
    if (!b || !b->alloc.mapped) return Errc::InvalidArgument;
    if (!(allocator_.properties().memoryTypes[b->alloc.memoryType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        r.memory = b->alloc.memory;
        r.offset = 0;
        r.size = VK_WHOLE_SIZE;
        (void)vkInvalidateMappedMemoryRanges(device_, 1, &r);
    }
    outPtr = b->alloc.mapped;
    return OkStatus;
}

void Backend::unmap_buffer(BufferHandle) noexcept {}

// =============================================================================
// Samplers e shaders
// =============================================================================
Result<SamplerHandle> Backend::create_sampler(const SamplerDesc& desc) noexcept {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = desc.magFilter == SamplerDesc::Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.minFilter = desc.minFilter == SamplerDesc::Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.mipmapMode = desc.mipmap == SamplerDesc::Mipmap::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                                 : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = to_vk(desc.wrapU);
    info.addressModeV = to_vk(desc.wrapV);
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    info.maxLod = VK_LOD_CLAMP_NONE;
    if (desc.maxAnisotropy > 1.0f && caps_.maxSamplerAnisotropy > 1.0f) {
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy = std::min(desc.maxAnisotropy, caps_.maxSamplerAnisotropy);
    }
    SamplerObject s;
    if (const Status st = check(vkCreateSampler(device_, &info, nullptr, &s.sampler), "vkCreateSampler"); !st.ok()) return st;
    return SamplerHandle{samplers_.add(std::move(s))};
}

void Backend::destroy_sampler(SamplerHandle h) noexcept {
    SamplerObject* s = samplers_.get(h.id);
    if (!s || s->shared) return;   // sampler de vídeo externo vive no cache de formato
    SamplerObject dead;
    samplers_.remove(h.id, dead);
    struct Node { Backend* b; VkSampler s; };
    defer_until_gpu_done([](void* p) {
        auto* n = static_cast<Node*>(p);
        vkDestroySampler(n->b->device(), n->s, nullptr);
        delete n;
    }, new Node{this, dead.sampler});
}

Result<ShaderHandle> Backend::create_shader(const ShaderDesc& desc) noexcept {
    if (!desc.spirv || desc.spirvBytes < 20 || desc.spirvBytes % 4 != 0) {
        return Status{Errc::InvalidArgument, "SPIR-V invalido"};
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = desc.spirvBytes;
    info.pCode = desc.spirv;
    ShaderModule m;
    m.stage = desc.stage;
    if (const Status s = check(vkCreateShaderModule(device_, &info, nullptr, &m.module), "vkCreateShaderModule"); !s.ok()) {
        return Status{Errc::ShaderCompileFailed, desc.debugName};
    }
    set_object_name(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<u64>(m.module), desc.debugName);
    return ShaderHandle{shaders_.add(std::move(m))};
}

void Backend::destroy_shader(ShaderHandle h) noexcept {
    ShaderModule m;
    if (!shaders_.remove(h.id, m)) return;
    // Um módulo pode ser destruído logo depois de criar os pipelines que o
    // usam: o pipeline não depende dele. Mesmo assim, adiado por simetria.
    struct Node { Backend* b; VkShaderModule m; };
    defer_until_gpu_done([](void* p) {
        auto* n = static_cast<Node*>(p);
        vkDestroyShaderModule(n->b->device(), n->m, nullptr);
        delete n;
    }, new Node{this, m.module});
}

// =============================================================================
// Layout universal de pipeline (um por sampler imutável)
// =============================================================================
VkPipelineLayout Backend::pipeline_layout(u64 immutableSampler, VkDescriptorSetLayout& outSet) noexcept {
    if (auto it = layouts_.find(immutableSampler); it != layouts_.end()) {
        outSet = setLayouts_[immutableSampler];
        return it->second;
    }
    VkSampler immutable = VK_NULL_HANDLE;
    if (immutableSampler) {
        const SamplerObject* s = samplers_.get(immutableSampler);
        if (!s) return VK_NULL_HANDLE;
        immutable = s->sampler;
    }

    const VkShaderStageFlags all = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutBinding b[binding::kBindingCount]{};
    for (u32 i = 0; i < binding::kTextureSlots; ++i) {
        b[i].binding = i;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].descriptorCount = 1;
        // Vértice também: alvos de morph podem vir de textura.
        b[i].stageFlags = all;
    }
    if (immutable) b[0].pImmutableSamplers = &immutable;
    const u32 u = binding::kUniform;
    b[u].binding = binding::kUniform;
    b[u].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    b[u].descriptorCount = 1;
    b[u].stageFlags = all;
    for (u32 i = 0; i < binding::kStorageImageSlots; ++i) {
        const u32 k = binding::kStorageImage0 + i;
        b[k].binding = k;
        b[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[k].descriptorCount = 1;
        b[k].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    }
    const u32 sb = binding::kStorageBuffer;
    b[sb].binding = binding::kStorageBuffer;
    b[sb].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b[sb].descriptorCount = 1;
    b[sb].stageFlags = all;

    VkDescriptorSetLayoutCreateInfo si{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    si.bindingCount = binding::kBindingCount;
    si.pBindings = b;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device_, &si, nullptr, &setLayout) != VK_SUCCESS) return VK_NULL_HANDLE;

    VkPushConstantRange push{all, 0, binding::kPushConstantBytes};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &setLayout;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &push;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device_, &li, nullptr, &layout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(device_, setLayout, nullptr);
        return VK_NULL_HANDLE;
    }
    layouts_[immutableSampler] = layout;
    setLayouts_[immutableSampler] = setLayout;
    outSet = setLayout;
    return layout;
}

// =============================================================================
// Render pass e framebuffer
//
// Layout inicial e final = COLOR_ATTACHMENT_OPTIMAL: as transições de layout são
// do FrameGraph (barreiras explícitas antes e depois). As dependências externas
// cobrem o caso de dois passes seguidos escrevendo o mesmo alvo (o segundo com
// LOAD), que não troca de estado e por isso não gera barreira no grafo.
// =============================================================================
VkRenderPass Backend::render_pass(VkFormat format, LoadOp load) noexcept {
    const u64 key = static_cast<u64>(format) | (static_cast<u64>(load) << 32);
    if (auto it = renderPasses_.find(key); it != renderPasses_.end()) return it->second;

    VkAttachmentDescription a{};
    a.format = format;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = load == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR
             : load == LoadOp::Load  ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;

    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1;
    info.pAttachments = &a;
    info.subpassCount = 1;
    info.pSubpasses = &sub;
    info.dependencyCount = 2;
    info.pDependencies = deps;
    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device_, &info, nullptr, &rp) != VK_SUCCESS) return VK_NULL_HANDLE;
    renderPasses_[key] = rp;
    return rp;
}

VkRenderPass Backend::render_pass(VkFormat format, LoadOp load, VkFormat depth, LoadOp depthLoad,
                                  bool storeDepth) noexcept {
    if (depth == VK_FORMAT_UNDEFINED) return render_pass(format, load);
    const u64 key = (1ull << 63) | static_cast<u64>(format) | (static_cast<u64>(load) << 20)
                  | (static_cast<u64>(depth) << 24) | (static_cast<u64>(depthLoad) << 44)
                  | (static_cast<u64>(storeDepth) << 48);
    if (auto it = renderPasses_.find(key); it != renderPasses_.end()) return it->second;

    auto load_op = [](LoadOp l) {
        return l == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR
             : l == LoadOp::Load  ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    };
    VkAttachmentDescription att[2]{};
    u32 count = 0;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const bool hasColor = format != VK_FORMAT_UNDEFINED;
    if (hasColor) {
        VkAttachmentDescription& a = att[count++];
        a.format = format;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = load_op(load);
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    VkAttachmentReference depthRef{count, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    {
        VkAttachmentDescription& a = att[count++];
        a.format = depth;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = load_op(depthLoad);
        a.storeOp = storeDepth ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = hasColor ? 1 : 0;
    sub.pColorAttachments = hasColor ? &colorRef : nullptr;
    sub.pDepthStencilAttachment = &depthRef;

    const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                                      | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                      | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const VkAccessFlags access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                               | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                               | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkSubpassDependency deps[2]{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = stages;
    deps[0].dstStageMask = stages;
    deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = access;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = stages;
    deps[1].dstStageMask = stages;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = access;

    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = count;
    info.pAttachments = att;
    info.subpassCount = 1;
    info.pSubpasses = &sub;
    info.dependencyCount = 2;
    info.pDependencies = deps;
    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device_, &info, nullptr, &rp) != VK_SUCCESS) return VK_NULL_HANDLE;
    renderPasses_[key] = rp;
    return rp;
}

VkFramebuffer Backend::framebuffer(Texture* color, Texture& depth, u64 depthId, LoadOp load, LoadOp depthLoad,
                                   bool storeDepth) noexcept {
    // O framebuffer mora na textura de cor (ou na de profundidade, num passe
    // só de profundidade), indexado pela profundidade e pelos load ops.
    Texture& owner = color ? *color : depth;
    const u32 key = static_cast<u32>(load) | (static_cast<u32>(depthLoad) << 4) | (static_cast<u32>(storeDepth) << 8);
    for (const Texture::DepthFramebuffer& d : owner.depthFramebuffers) {
        if (d.depth == depthId && d.key == key) return d.fb;
    }
    VkImageView views[2];
    u32 n = 0;
    if (color) views[n++] = color->view;
    views[n++] = depth.view;
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = render_pass(color ? color->format : VK_FORMAT_UNDEFINED, load, depth.format, depthLoad, storeDepth);
    info.attachmentCount = n;
    info.pAttachments = views;
    info.width = depth.desc.width;
    info.height = depth.desc.height;
    info.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    if (!info.renderPass || vkCreateFramebuffer(device_, &info, nullptr, &fb) != VK_SUCCESS) return VK_NULL_HANDLE;
    owner.depthFramebuffers.push_back(Texture::DepthFramebuffer{depthId, key, fb});
    return fb;
}

VkFramebuffer Backend::framebuffer(Texture& t, LoadOp load) noexcept {
    const u32 i = static_cast<u32>(load);
    if (t.framebuffers[i]) return t.framebuffers[i];
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = render_pass(t.format, load);
    info.attachmentCount = 1;
    info.pAttachments = &t.view;
    info.width = t.desc.width;
    info.height = t.desc.height;
    info.layers = 1;
    if (vkCreateFramebuffer(device_, &info, nullptr, &t.framebuffers[i]) != VK_SUCCESS) return VK_NULL_HANDLE;
    return t.framebuffers[i];
}

// =============================================================================
// Pipelines
// =============================================================================
Result<PipelineHandle> Backend::create_pipeline(const PipelineDesc& desc) noexcept {
    PipelineObject p;
    p.layout = pipeline_layout(desc.immutableSampler0.id, p.setLayout);
    if (!p.layout) return Status{Errc::PipelineCompileFailed, "layout de pipeline"};

    if (desc.isCompute) {
        const ShaderModule* cs = shaders_.get(desc.computeShader.id);
        if (!cs) return Status{Errc::InvalidArgument, "shader de compute ausente"};
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = cs->module;
        info.stage.pName = "main";
        info.layout = p.layout;
        if (vkCreateComputePipelines(device_, pipelineCache_, 1, &info, nullptr, &p.pipeline) != VK_SUCCESS) {
            return Status{Errc::PipelineCompileFailed, desc.debugName};
        }
        p.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        set_object_name(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(p.pipeline), desc.debugName);
        return PipelineHandle{pipelines_.add(std::move(p))};
    }

    const ShaderModule* vs = shaders_.get(desc.vertexShader.id);
    const ShaderModule* fs = shaders_.get(desc.fragmentShader.id);
    if (!vs || !fs) return Status{Errc::InvalidArgument, "shader grafico ausente"};

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs->module;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs->module;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkVertexInputAttributeDescription attrs[VertexLayout::kMaxAttributes]{};
    VkVertexInputBindingDescription binds[VertexLayout::kMaxBindings]{};
    const VertexLayout& vl = desc.vertexLayout;
    for (u32 i = 0; i < vl.attributeCount; ++i) {
        attrs[i].location = vl.attributes[i].location;
        attrs[i].binding = vl.attributes[i].binding;
        attrs[i].format = to_vk(vl.attributes[i].format);
        attrs[i].offset = vl.attributes[i].offset;
    }
    u32 bindCount = 0;
    for (u32 b = 0; b < vl.bindingCount; ++b) {
        if (vl.bindings[b].stride == 0) continue;
        binds[bindCount].binding = b;
        binds[bindCount].stride = vl.bindings[b].stride;
        binds[bindCount].inputRate = vl.bindings[b].perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        ++bindCount;
    }
    vertexInput.vertexAttributeDescriptionCount = vl.attributeCount;
    vertexInput.pVertexAttributeDescriptions = attrs;
    vertexInput.vertexBindingDescriptionCount = bindCount;
    vertexInput.pVertexBindingDescriptions = binds;
    VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthState.depthTestEnable = desc.depth.test ? VK_TRUE : VK_FALSE;
    depthState.depthWriteEnable = desc.depth.write ? VK_TRUE : VK_FALSE;
    depthState.depthCompareOp = to_vk(desc.depth.compare);
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = to_vk(desc.topology);
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = desc.cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT
                    : desc.cull == CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_NONE;
    raster.frontFace = desc.frontFaceCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    if (desc.depth.biasConstant != 0.0f || desc.depth.biasSlope != 0.0f) {
        raster.depthBiasEnable = VK_TRUE;
        raster.depthBiasConstantFactor = desc.depth.biasConstant;
        raster.depthBiasSlopeFactor = desc.depth.biasSlope;
    }
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Alvos PRÉ-MULTIPLICADOS: Normal = src + dst*(1-srcA); Add = src + dst.
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                         | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (desc.blendEnabled) {
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        if (desc.blend == BlendMode::Add) {
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        } else {
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }
    }
    VkPipelineColorBlendStateCreateInfo blendState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blendState.attachmentCount = desc.depthOnly ? 0 : 1;
    blendState.pAttachments = desc.depthOnly ? nullptr : &blend;

    const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blendState;
    info.pDepthStencilState = desc.hasDepth ? &depthState : nullptr;
    info.pDynamicState = &dynamic;
    info.layout = p.layout;
    // Qualquer render pass com o mesmo formato é compatível: o load op não
    // entra na compatibilidade, então um pipeline serve para CLEAR, LOAD e
    // DONT_CARE.
    info.renderPass = desc.hasDepth
                    ? render_pass(desc.depthOnly ? VK_FORMAT_UNDEFINED : to_vk(desc.colorFormat), LoadOp::Load,
                                  to_vk(desc.depthFormat), LoadOp::Load, true)
                    : render_pass(to_vk(desc.colorFormat), LoadOp::Load);
    info.subpass = 0;
    if (!info.renderPass) return Status{Errc::PipelineCompileFailed, "render pass"};
    if (vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &info, nullptr, &p.pipeline) != VK_SUCCESS) {
        return Status{Errc::PipelineCompileFailed, desc.debugName};
    }
    p.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    set_object_name(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(p.pipeline), desc.debugName);
    return PipelineHandle{pipelines_.add(std::move(p))};
}

void Backend::destroy_pipeline(PipelineHandle h) noexcept {
    PipelineObject p;
    if (!pipelines_.remove(h.id, p)) return;
    struct Node { Backend* b; VkPipeline p; };
    defer_until_gpu_done([](void* ptr) {
        auto* n = static_cast<Node*>(ptr);
        vkDestroyPipeline(n->b->device(), n->p, nullptr);
        delete n;
    }, new Node{this, p.pipeline});
}

// =============================================================================
// Upload e leitura de volta
// =============================================================================
Status Backend::upload_texture(TextureHandle dst, const void* data, u32 bytesPerRow) noexcept {
    Texture* t = textures_.get(dst.id);
    if (!t || !data) return Errc::InvalidArgument;
    const u32 bpp = texel_bytes(t->format);
    const u32 w = t->desc.width, h = t->desc.height;
    const u32 rowBytes = w * bpp;
    const u32 pitch = bytesPerRow ? bytesPerRow : rowBytes;
    if (pitch < rowBytes || pitch % bpp != 0) return Status{Errc::InvalidArgument, "passo de linha invalido"};
    const VkDeviceSize total = static_cast<VkDeviceSize>(rowBytes) * h;

    auto record = [&](VkCommandBuffer cmd, VkBuffer src, VkDeviceSize offset) {
        transition(cmd, *t, ResourceState::TransferDst, true);
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {w, h, 1};
        vkCmdCopyBufferToImage(cmd, src, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        // Fica pronta para amostrar: quem sobe textura (LUT, imagem, plano de
        // vídeo) a lê em seguida num passe, fora do FrameGraph.
        transition(cmd, *t, ResourceState::ShaderRead, false);
    };

    if (current_ && !commands_.in_render_pass()) {
        // Frame aberto: vai para o anel de staging do frame e a cópia entra na
        // lista ANTES dos passes que leem a textura.
        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceSize off = 0;
        void* ptr = nullptr;
        if (!current_->staging.allocate(total, 16, buf, off, ptr)) return Errc::OutOfMemory;
        const u8* src = static_cast<const u8*>(data);
        u8* d = static_cast<u8*>(ptr);
        if (pitch == rowBytes) std::memcpy(d, src, total);
        else for (u32 y = 0; y < h; ++y) std::memcpy(d + static_cast<usize>(y) * rowBytes, src + static_cast<usize>(y) * pitch, rowBytes);
        record(current_->cmd, buf, off);
        uploadBytesFrame_ += total;
        return OkStatus;
    }

    // Fora de frame: staging próprio, submissão e espera.
    BufferDesc sd;
    sd.bytes = total;
    sd.usage = BufferUsage::TransferSrc;
    sd.access = MemoryAccess::Upload;
    auto staging = create_buffer(sd);
    if (!staging.ok()) return staging.status();
    Buffer* s = buffers_.get(staging->id);
    const u8* src = static_cast<const u8*>(data);
    u8* d = static_cast<u8*>(s->alloc.mapped);
    for (u32 y = 0; y < h; ++y) std::memcpy(d + static_cast<usize>(y) * rowBytes, src + static_cast<usize>(y) * pitch, rowBytes);
    struct Ctx { decltype(record)* rec; VkBuffer buf; } ctx{&record, s->buffer};
    const Status st = submit_immediate([](Backend&, VkCommandBuffer cmd, void* p) {
        auto* c = static_cast<Ctx*>(p);
        (*c->rec)(cmd, c->buf, 0);
    }, &ctx);
    Buffer dead;
    if (buffers_.remove(staging->id, dead)) destroy_buffer_now(dead);
    return st;
}

Status Backend::upload_texture_level(TextureHandle dst, u32 mipLevel, u32 layer, const void* data,
                                     usize bytes) noexcept {
    Texture* t = textures_.get(dst.id);
    if (!t || !data || bytes == 0) return Errc::InvalidArgument;
    const u32 levels = std::max(1u, t->desc.mipLevels);
    const u32 layers = t->desc.cube ? 6u : std::max(1u, t->desc.layers);
    if (mipLevel >= levels || layer >= layers) return Status{Errc::OutOfRange, "mip/camada fora da textura"};
    const u32 w = std::max(1u, t->desc.width >> mipLevel);
    const u32 h = std::max(1u, t->desc.height >> mipLevel);
    const bool block = is_block_compressed(t->desc.format);
    const usize expected = block ? static_cast<usize>((w + 3) / 4) * ((h + 3) / 4) * 16
                                 : static_cast<usize>(w) * h * t->desc.bytes_per_pixel();
    if (bytes < expected) return Status{Errc::InvalidArgument, "dados menores que o nivel"};

    struct Ctx { Backend* be; Texture* t; VkBuffer buf; VkDeviceSize off; u32 mip, layer, w, h; } ctx{};
    auto record = [](Backend& be, VkCommandBuffer cmd, void* p) {
        auto* c = static_cast<Ctx*>(p);
        // Barreira só do subrecurso: os outros níveis mantêm o conteúdo.
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = c->t->image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, c->mip, 1, c->layer, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &b);
        VkBufferImageCopy region{};
        region.bufferOffset = c->off;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, c->mip, c->layer, 1};
        region.imageExtent = {c->w, c->h, 1};
        vkCmdCopyBufferToImage(cmd, c->buf, c->t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &b);
        c->t->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        c->t->state = ResourceState::ShaderRead;
        (void)be;
    };
    // Sempre por submissão própria: o import de 3D roda fora do frame, e um
    // nível 4K não cabe no anel de staging do frame.
    BufferDesc sd;
    sd.bytes = expected;
    sd.usage = BufferUsage::TransferSrc;
    sd.access = MemoryAccess::Upload;
    auto staging = create_buffer(sd);
    if (!staging.ok()) return staging.status();
    Buffer* s = buffers_.get(staging->id);
    std::memcpy(s->alloc.mapped, data, expected);
    ctx = Ctx{this, t, s->buffer, 0, mipLevel, layer, w, h};
    const Status st = submit_immediate(record, &ctx);
    Buffer dead;
    if (buffers_.remove(staging->id, dead)) destroy_buffer_now(dead);
    return st;
}

Status Backend::generate_mipmaps(TextureHandle texture) noexcept {
    Texture* t = textures_.get(texture.id);
    if (!t) return Errc::InvalidArgument;
    const u32 levels = std::max(1u, t->desc.mipLevels);
    if (levels <= 1) return OkStatus;
    if (is_block_compressed(t->desc.format)) return Status{Errc::NotSupported, "mips de textura comprimida vem do arquivo"};
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(physical_, t->format, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
        || !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)
        || !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
        return Status{Errc::NotSupported, "formato sem blit linear"};
    }
    struct Ctx { Texture* t; u32 levels; u32 layers; } ctx{t, levels, t->desc.cube ? 6u : std::max(1u, t->desc.layers)};
    return submit_immediate([](Backend&, VkCommandBuffer cmd, void* p) {
        auto* c = static_cast<Ctx*>(p);
        Texture& tx = *c->t;
        auto barrier = [&](u32 mip, VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst,
                           VkPipelineStageFlags s0, VkPipelineStageFlags s1) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.srcAccessMask = src;
            b.dstAccessMask = dst;
            b.oldLayout = from;
            b.newLayout = to;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = tx.image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, c->layers};
            vkCmdPipelineBarrier(cmd, s0, s1, 0, 0, nullptr, 0, nullptr, 1, &b);
        };
        // Nível 0 vem de amostragem (subido antes); vira origem de blit.
        barrier(0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
        i32 w = static_cast<i32>(tx.desc.width), h = static_cast<i32>(tx.desc.height);
        for (u32 m = 1; m < c->levels; ++m) {
            const i32 nw = std::max(1, w / 2), nh = std::max(1, h / 2);
            barrier(m, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, c->layers};
            blit.srcOffsets[1] = {w, h, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, c->layers};
            blit.dstOffsets[1] = {nw, nh, 1};
            vkCmdBlitImage(cmd, tx.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, tx.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            barrier(m, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);
            w = nw;
            h = nh;
        }
        for (u32 m = 0; m < c->levels; ++m) {
            barrier(m, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
        tx.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        tx.state = ResourceState::ShaderRead;
    }, &ctx);
}

Status Backend::read_texture(TextureHandle src, void* outData, u32 bytesPerRow) noexcept {
    Texture* t = textures_.get(src.id);
    if (!t || !outData) return Errc::InvalidArgument;
    const u32 bpp = texel_bytes(t->format);
    const u32 w = t->desc.width, h = t->desc.height;
    const u32 rowBytes = w * bpp;
    const VkDeviceSize total = static_cast<VkDeviceSize>(rowBytes) * h;

    BufferDesc rd;
    rd.bytes = total;
    rd.usage = BufferUsage::TransferDst;
    rd.access = MemoryAccess::Readback;
    auto readback = create_buffer(rd);
    if (!readback.ok()) return readback.status();
    Buffer* b = buffers_.get(readback->id);

    struct Ctx { Texture* t; VkBuffer buf; u32 w, h; } ctx{t, b->buffer, w, h};
    const Status st = submit_immediate([](Backend& be, VkCommandBuffer cmd, void* p) {
        auto* c = static_cast<Ctx*>(p);
        const ResourceState before = c->t->state;
        be.transition(cmd, *c->t, ResourceState::TransferSrc, false);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {c->w, c->h, 1};
        vkCmdCopyImageToBuffer(cmd, c->t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->buf, 1, &region);
        if (before != ResourceState::Undefined) be.transition(cmd, *c->t, before, false);
    }, &ctx);
    if (st.ok()) {
        void* mapped = nullptr;
        if (map_buffer(*readback, mapped).ok()) {
            const u32 pitch = bytesPerRow ? bytesPerRow : rowBytes;
            for (u32 y = 0; y < h; ++y) {
                std::memcpy(static_cast<u8*>(outData) + static_cast<usize>(y) * pitch,
                            static_cast<const u8*>(mapped) + static_cast<usize>(y) * rowBytes, rowBytes);
            }
        }
    }
    Buffer dead;
    if (buffers_.remove(readback->id, dead)) destroy_buffer_now(dead);
    return st;
}

Status Backend::submit_immediate(void (*record)(Backend&, VkCommandBuffer, void*), void* ctx) noexcept {
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pi.queueFamilyIndex = graphicsFamily_;
    VkCommandPool pool = VK_NULL_HANDLE;
    if (const Status s = check(vkCreateCommandPool(device_, &pi, nullptr, &pool), "vkCreateCommandPool"); !s.ok()) return s;
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    (void)vkAllocateCommandBuffers(device_, &ai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    record(*this, cmd, ctx);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(device_, &fi, nullptr, &fence);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VkResult r = vkQueueSubmit(queue_, 1, &si, fence);
    if (r == VK_SUCCESS) r = vkWaitForFences(device_, 1, &fence, VK_TRUE, 5'000'000'000ull);
    (void)note_device_lost(r);
    vkDestroyFence(device_, fence, nullptr);
    vkDestroyCommandPool(device_, pool, nullptr);
    return check(r, "submissao imediata");
}

// =============================================================================
// Zero-copy: AHardwareBuffer do MediaCodec → VkImage amostrável
// =============================================================================
Result<ExternalTexture> Backend::import_external_image(const ExternalImageDesc& img) noexcept {
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    if (!hasAhb_ || !hasYcbcr_) return Status{Errc::UnsupportedFeature, "aparelho sem importacao de AHardwareBuffer"};
    auto* ahb = static_cast<AHardwareBuffer*>(img.nativeHandle);
    if (!ahb) return Status{Errc::InvalidArgument, "buffer nulo"};

    const u64 colorKey = static_cast<u64>(img.matrix) | (img.fullRange ? 4ull : 0ull);

    // O ImageReader recicla um conjunto fixo de buffers: a importação é feita
    // uma vez por buffer e reaproveitada. Em regime, importar custa zero.
    if (auto it = importedByBuffer_.find(ahb); it != importedByBuffer_.end()) {
        Texture* cached = textures_.get(it->second);
        if (cached && cached->colorKey != colorKey) {
            // A cor do vídeo mudou (formato de saída novo): a view carrega a
            // conversão antiga e precisa ser refeita.
            destroy_texture(TextureHandle{it->second});
            cached = nullptr;
        }
        if (Texture* t = cached) {
            t->lastUsedFrame = frameNumber_;
            t->state = ResourceState::Undefined;   // conteúdo novo do decoder: nova aquisição
            t->layout = VK_IMAGE_LAYOUT_UNDEFINED;
            const SamplerObject* s = samplers_.get(t->ycbcrSampler);
            ExternalTexture out;
            out.texture = TextureHandle{it->second};
            out.sampler = SamplerHandle{t->ycbcrSampler};
            out.formatKey = s ? reinterpret_cast<u64>(s->conversion) : 0;
            out.rgb = t->externalRgb;
            return out;
        }
        importedByBuffer_.erase(ahb);
    }

    VkAndroidHardwareBufferFormatPropertiesANDROID fmt{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID props{VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
    props.pNext = &fmt;
    if (const Status s = check(vkGetAndroidHardwareBufferPropertiesANDROID(device_, ahb, &props),
                               "vkGetAndroidHardwareBufferPropertiesANDROID"); !s.ok()) {
        return s;
    }
    const bool externalFormat = fmt.format == VK_FORMAT_UNDEFINED;
    const u64 formatKey = (externalFormat ? fmt.externalFormat : (static_cast<u64>(fmt.format) | (1ull << 63)))
                        ^ (colorKey * 0x9E3779B97F4A7C15ull);
    // YCbCr de verdade: um formato multi-plano conhecido, ou um formato externo
    // para o qual o driver sugere um modelo YCbCr. Um formato RGB comum (ou um
    // externo com sugestão RGB_IDENTITY) já traz a cor convertida.
    const bool ycbcrFormat = fmt.format >= VK_FORMAT_G8B8G8R8_422_UNORM
                          && fmt.format <= VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM;
    const bool rgbContent = externalFormat
        ? fmt.suggestedYcbcrModel == VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY
        : !ycbcrFormat;
    const bool needsConversion = externalFormat || ycbcrFormat;

    // Conversão YCbCr por (formato, matriz, faixa). A matriz e a faixa são as
    // DO ARQUIVO, não a sugestão do driver: o gralloc de muitos aparelhos (e o
    // do emulador) sugere BT.601 cheio para qualquer vídeo. O modelo
    // RGB_IDENTITY com a conta no shader seria mais puro, mas há drivers que o
    // ignoram e convertem assim mesmo — a cor sairia convertida duas vezes.
    // Curva de transferência, primárias e tone map continuam no shader.
    u64 samplerId = 0;
    if (auto it = ycbcrByFormat_.find(formatKey); it != ycbcrByFormat_.end()) {
        samplerId = it->second;
    } else if (!needsConversion) {
        // RGB comum: sampler linear sem conversão (conversão YCbCr só é válida
        // para formatos YCbCr ou externos).
        AUREA_LOG_INFO("ahb: formato RGB %d (sem conversao YCbCr)", static_cast<int>(fmt.format));
        SamplerObject so;
        so.shared = true;
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.maxLod = 0.0f;
        if (const Status s = check(vkCreateSampler(device_, &si, nullptr, &so.sampler), "vkCreateSampler(ahb rgb)"); !s.ok()) {
            return s;
        }
        samplerId = samplers_.add(std::move(so));
        ycbcrByFormat_[formatKey] = samplerId;
    } else {
        AUREA_LOG_INFO("ahb: formato %d externo %llu modelo sugerido %d faixa %d features 0x%x%s",
                       static_cast<int>(fmt.format), static_cast<unsigned long long>(fmt.externalFormat),
                       static_cast<int>(fmt.suggestedYcbcrModel), static_cast<int>(fmt.suggestedYcbcrRange),
                       static_cast<unsigned>(fmt.formatFeatures), rgbContent ? " (conteudo RGB)" : "");
        VkExternalFormatANDROID ext{VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID};
        ext.externalFormat = externalFormat ? fmt.externalFormat : 0;
        const bool linear = (fmt.formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT) != 0;
        VkSamplerYcbcrConversionCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO};
        ci.pNext = externalFormat ? &ext : nullptr;
        ci.format = fmt.format;
        switch (img.matrix) {
            case YCbCrMatrix::BT601:  ci.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601; break;
            case YCbCrMatrix::BT2020: ci.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020; break;
            default:                  ci.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709; break;
        }
        // Buffer que já é RGB (externo com sugestão RGB_IDENTITY): sem matriz.
        if (rgbContent) ci.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
        ci.ycbcrRange = img.fullRange || rgbContent ? VK_SAMPLER_YCBCR_RANGE_ITU_FULL : VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
        ci.components = fmt.samplerYcbcrConversionComponents;
        ci.xChromaOffset = fmt.suggestedXChromaOffset;
        ci.yChromaOffset = fmt.suggestedYChromaOffset;
        ci.chromaFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        ci.forceExplicitReconstruction = VK_FALSE;
        SamplerObject so;
        so.shared = true;
        if (const Status s = check(vkCreateSamplerYcbcrConversion(device_, &ci, nullptr, &so.conversion),
                                   "vkCreateSamplerYcbcrConversion"); !s.ok()) {
            return s;
        }
        VkSamplerYcbcrConversionInfo convInfo{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
        convInfo.conversion = so.conversion;
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.pNext = &convInfo;
        si.magFilter = si.minFilter = ci.chromaFilter;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.maxLod = 0.0f;
        if (const Status s = check(vkCreateSampler(device_, &si, nullptr, &so.sampler), "vkCreateSampler(ycbcr)"); !s.ok()) {
            vkDestroySamplerYcbcrConversion(device_, so.conversion, nullptr);
            return s;
        }
        samplerId = samplers_.add(std::move(so));
        ycbcrByFormat_[formatKey] = samplerId;
    }
    const SamplerObject* conv = samplers_.get(samplerId);

    AHardwareBuffer_Desc ad{};
    AHardwareBuffer_describe(ahb, &ad);

    Texture t;
    t.external = true;
    t.format = fmt.format;
    t.desc.width = ad.width;
    t.desc.height = ad.height;
    t.desc.format = SurfaceFormat::RGBA8;
    t.desc.sampled = true;
    t.ycbcrSampler = samplerId;
    t.nativeBuffer = ahb;
    t.externalRgb = true;
    t.colorKey = colorKey;

    VkExternalFormatANDROID extFmt{VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID};
    extFmt.externalFormat = externalFormat ? fmt.externalFormat : 0;
    VkExternalMemoryImageCreateInfo extMem{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    extMem.pNext = &extFmt;
    extMem.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.pNext = &extMem;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt.format;
    ii.extent = {ad.width, ad.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (const Status s = check(vkCreateImage(device_, &ii, nullptr, &t.image), "vkCreateImage(ahb)"); !s.ok()) return s;

    VkImportAndroidHardwareBufferInfoANDROID imp{VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
    imp.buffer = ahb;
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = t.image;
    imp.pNext = &dedicated;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.pNext = &imp;
    ai.allocationSize = props.allocationSize;
    ai.memoryTypeIndex = 0;
    while (ai.memoryTypeIndex < 32 && !(props.memoryTypeBits & (1u << ai.memoryTypeIndex))) ++ai.memoryTypeIndex;
    if (const Status s = check(vkAllocateMemory(device_, &ai, nullptr, &t.importedMemory), "vkAllocateMemory(ahb)"); !s.ok()) {
        vkDestroyImage(device_, t.image, nullptr);
        return s;
    }
    if (const Status s = check(vkBindImageMemory(device_, t.image, t.importedMemory, 0), "vkBindImageMemory(ahb)"); !s.ok()) {
        vkFreeMemory(device_, t.importedMemory, nullptr);
        vkDestroyImage(device_, t.image, nullptr);
        return s;
    }

    VkSamplerYcbcrConversionInfo convInfo{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    convInfo.conversion = conv->conversion;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.pNext = conv->conversion ? &convInfo : nullptr;
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (const Status s = check(vkCreateImageView(device_, &vi, nullptr, &t.view), "vkCreateImageView(ahb)"); !s.ok()) {
        vkFreeMemory(device_, t.importedMemory, nullptr);
        vkDestroyImage(device_, t.image, nullptr);
        return s;
    }
    // A importação segura o buffer enquanto estiver no cache: o ImageReader
    // pode soltá-lo, mas a VkImage continua válida até sairmos daqui.
    AHardwareBuffer_acquire(ahb);
    t.lastUsedFrame = frameNumber_;
    const u64 id = textures_.add(std::move(t));
    importedByBuffer_[ahb] = id;

    ExternalTexture out;
    out.texture = TextureHandle{id};
    out.sampler = SamplerHandle{samplerId};
    out.formatKey = conv->conversion ? reinterpret_cast<u64>(conv->conversion) : formatKey;
    out.rgb = true;
    return out;
#else
    (void)img;
    return Status{Errc::NotSupported, "importacao de imagem externa so existe no Android"};
#endif
}

void Backend::release_external_image(TextureHandle imported) noexcept { destroy_texture(imported); }

} // namespace aurea::vk

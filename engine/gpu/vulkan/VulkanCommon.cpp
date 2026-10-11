#include "VulkanBackend.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/platform/AddressSpace.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace aurea::vk {

// =============================================================================
// Resultado e formatos
// =============================================================================
const char* result_name(VkResult r) noexcept {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        default: return "VkResult desconhecido";
    }
}

Status check(VkResult r, const char* what) noexcept {
    if (r == VK_SUCCESS) return OkStatus;
    AUREA_LOG_ERROR("vulkan: %s -> %s", what, result_name(r));
    switch (r) {
        case VK_ERROR_DEVICE_LOST: return Status{Errc::DeviceLost, what};
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return Status{Errc::OutOfDeviceMemory, what};
        case VK_ERROR_OUT_OF_HOST_MEMORY: return Status{Errc::OutOfMemory, what};
        case VK_ERROR_SURFACE_LOST_KHR:
        case VK_ERROR_OUT_OF_DATE_KHR: return Status{Errc::SurfaceLost, what};
        case VK_TIMEOUT: return Status{Errc::Timeout, what};
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return Status{Errc::UnsupportedFormat, what};
        case VK_ERROR_EXTENSION_NOT_PRESENT:
        case VK_ERROR_FEATURE_NOT_PRESENT: return Status{Errc::UnsupportedFeature, what};
        default: return Status{Errc::InvalidState, what};
    }
}

VkFormat to_vk(SurfaceFormat f) noexcept {
    switch (f) {
        case SurfaceFormat::R8:       return VK_FORMAT_R8_UNORM;
        case SurfaceFormat::RG8:      return VK_FORMAT_R8G8_UNORM;
        case SurfaceFormat::RGBA8:    return VK_FORMAT_R8G8B8A8_UNORM;
        case SurfaceFormat::BGRA8:    return VK_FORMAT_B8G8R8A8_UNORM;
        case SurfaceFormat::R16F:     return VK_FORMAT_R16_SFLOAT;
        case SurfaceFormat::RG16F:    return VK_FORMAT_R16G16_SFLOAT;
        case SurfaceFormat::RGBA16F:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case SurfaceFormat::R32F:     return VK_FORMAT_R32_SFLOAT;
        case SurfaceFormat::RGBA32F:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case SurfaceFormat::R16:      return VK_FORMAT_R16_UNORM;
        case SurfaceFormat::RG16:     return VK_FORMAT_R16G16_UNORM;
        case SurfaceFormat::Depth24:  return VK_FORMAT_X8_D24_UNORM_PACK32;
        case SurfaceFormat::Depth32F: return VK_FORMAT_D32_SFLOAT;
        case SurfaceFormat::RGBA8_sRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case SurfaceFormat::BC7:        return VK_FORMAT_BC7_UNORM_BLOCK;
        case SurfaceFormat::BC7_sRGB:   return VK_FORMAT_BC7_SRGB_BLOCK;
        case SurfaceFormat::ETC2_RGBA8: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
        case SurfaceFormat::ETC2_RGBA8_sRGB: return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
        case SurfaceFormat::ASTC4x4:    return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case SurfaceFormat::ASTC4x4_sRGB: return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
}

VkFormat to_vk(VertexFormat f) noexcept {
    switch (f) {
        case VertexFormat::Float:       return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::Float2:      return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3:      return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4:      return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::UByte4Norm:  return VK_FORMAT_R8G8B8A8_UNORM;
        case VertexFormat::UShort2Norm: return VK_FORMAT_R16G16_UNORM;
        case VertexFormat::UShort4Norm: return VK_FORMAT_R16G16B16A16_UNORM;
        case VertexFormat::UByte4:      return VK_FORMAT_R8G8B8A8_UINT;
        case VertexFormat::UShort4:     return VK_FORMAT_R16G16B16A16_UINT;
        case VertexFormat::Short4Norm:  return VK_FORMAT_R16G16B16A16_SNORM;
    }
    return VK_FORMAT_R32G32B32_SFLOAT;
}

VkCompareOp to_vk(CompareOp c) noexcept {
    switch (c) {
        case CompareOp::Never:          return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:           return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:        return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS_OR_EQUAL;
}

bool is_depth_format(VkFormat f) noexcept {
    return f == VK_FORMAT_D32_SFLOAT || f == VK_FORMAT_X8_D24_UNORM_PACK32 || f == VK_FORMAT_D16_UNORM
        || f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkImageAspectFlags aspect_of(VkFormat f) noexcept {
    return is_depth_format(f) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

SurfaceFormat from_vk(VkFormat f) noexcept {
    switch (f) {
        case VK_FORMAT_B8G8R8A8_UNORM: return SurfaceFormat::BGRA8;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return SurfaceFormat::RGBA16F;
        default: return SurfaceFormat::RGBA8;
    }
}

// =============================================================================
// MemoryAllocator
//
// Blocos grandes por tipo de memória, subalocados por primeiro encaixe com
// coalescência na liberação. Todo deslocamento é alinhado também à
// `bufferImageGranularity`: é conservador (gasta alguns KB por bloco) e elimina
// a classe inteira de bug de buffer e imagem vizinhos no mesmo bloco se
// corrompendo em GPU que exige a granularidade.
// =============================================================================
namespace {
// Full-quality mobile scenes often have several image memory types. A 64 MiB
// block per type left >90 MiB unused and refused an otherwise fitting frame.
// A few small live resources can pin several mostly empty 16 MiB slabs.
// Small pins must not strand a 4 MiB block after a larger neighbour retires.
// Blocks follow the request size (1/2/4 MiB), and reuse stays within that class.
// First-fit across classes let 6-74 KiB persistent pins keep entire 2/4 MiB
// transient slabs alive after a real fence, despite their much smaller tier.
// Existing allocations never move; resources above 2 MiB keep exact dedicated
// driver backing. A separate class records the request, not alignment padding.
// This reduces physical reservation without changing images or crediting
// deferred releases before their GPU fence has completed.
constexpr VkDeviceSize kDeviceBlock = 4ull * 1024 * 1024;
constexpr VkDeviceSize kHostBlock   = 4ull * 1024 * 1024;

VkDeviceSize align_up(VkDeviceSize v, VkDeviceSize a) noexcept { return a ? (v + a - 1) / a * a : v; }
} // namespace

void MemoryAllocator::initialize(VkPhysicalDevice physical, VkDevice device) noexcept {
    device_ = device;
    vkGetPhysicalDeviceMemoryProperties(physical, &props_);
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(physical, &p);
    granularity_ = std::max<VkDeviceSize>(1, p.limits.bufferImageGranularity);

    // Memória unificada (celular): algum tipo é device-local E host-visible
    // no heap principal.
    unified_ = false;
    for (u32 i = 0; i < props_.memoryTypeCount; ++i) {
        const VkMemoryPropertyFlags f = props_.memoryTypes[i].propertyFlags;
        if ((f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            const VkMemoryHeap& heap = props_.memoryHeaps[props_.memoryTypes[i].heapIndex];
            if (heap.size >= 512ull * 1024 * 1024) unified_ = true;
        }
    }
}

void MemoryAllocator::shutdown() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Block& b : blocks_) {
        if (b.mapped) vkUnmapMemory(device_, b.memory);
        if (b.memory) vkFreeMemory(device_, b.memory, nullptr);
    }
    blocks_.clear();
    reserved_ = used_ = 0;
    allocations_ = 0;
}

u32 MemoryAllocator::find_type(u32 bits, VkMemoryPropertyFlags required,
                               VkMemoryPropertyFlags preferred) const noexcept {
    const VkMemoryPropertyFlags want = required | preferred;
    for (u32 i = 0; i < props_.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (props_.memoryTypes[i].propertyFlags & want) == want) return i;
    }
    for (u32 i = 0; i < props_.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (props_.memoryTypes[i].propertyFlags & required) == required) return i;
    }
    return kInvalidIndex;
}

Allocation MemoryAllocator::allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags required,
                                     VkMemoryPropertyFlags preferred, bool dedicated,
                                     const char* debugName, bool hostPointer) noexcept {
    Allocation out;
    const u32 type = find_type(req.memoryTypeBits, required, preferred);
    if (type == kInvalidIndex) {
        AUREA_LOG_ERROR("memoria: nenhum tipo compativel para '%s'", debugName ? debugName : "?");
        return out;
    }
    const VkMemoryPropertyFlags flags = props_.memoryTypes[type].propertyFlags;
    const bool hostVisible = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    // 32 bits (APK armeabi-v7a): Mali/PowerVR declaram TODA a memória
    // host-visible; mapear cada bloco e cada textura dedicada (alvo 1080p
    // RGBA16F = 16 MB, quadro 4K...) esgotava os ~3 GB de endereços do
    // processo. Lá só se mapeia o que alguém escreve pela CPU.
    // 64 bits: mapeia tudo que é host-visible, como sempre.
    const bool mapNow = hostVisible && (hostPointer || address_space::map_gpu_memory_eagerly());
    const VkDeviceSize maximumBlockSize = hostVisible ? kHostBlock : kDeviceBlock;
    const VkDeviceSize tierSize = req.size <= (512ull << 10) ? (1ull << 20)
        : req.size <= (1ull << 20) ? (2ull << 20) : maximumBlockSize;
    const VkDeviceSize blockSize = std::max(tierSize, align_up(req.size, granularity_));
    const VkDeviceSize align = std::max(req.alignment, granularity_);

    std::lock_guard<std::mutex> lock(mutex_);

    // Grande demais para bloco: alocação dedicada (texturas 4K de vídeo,
    // anéis grandes). Subalocar isso fragmentaria o bloco inteiro.
    if (dedicated || req.size > maximumBlockSize / 2) {
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.allocationSize = req.size;
        info.memoryTypeIndex = type;
        if (vkAllocateMemory(device_, &info, nullptr, &out.memory) != VK_SUCCESS) {
            AUREA_LOG_ERROR("memoria: dedicada de %llu bytes falhou (%s)",
                            static_cast<unsigned long long>(req.size), debugName ? debugName : "?");
            out.memory = VK_NULL_HANDLE;
            return out;
        }
        if (mapNow) (void)vkMapMemory(device_, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped);
        out.size = req.size;
        out.memoryType = type;
        out.block = kInvalidIndex;
        reserved_ += req.size;
        used_ += req.size;
        ++allocations_;
        return out;
    }

    for (u32 attempt = 0; attempt < 2; ++attempt) {
        for (u32 bi = 0; bi < blocks_.size(); ++bi) {
            Block& b = blocks_[bi];
            if (!b.memory || b.memoryType != type || b.sizeClass != tierSize) continue;
            for (usize r = 0; r < b.free.size(); ++r) {
                Range& fr = b.free[r];
                const VkDeviceSize start = align_up(fr.offset, align);
                const VkDeviceSize end = start + align_up(req.size, granularity_);
                if (end > fr.offset + fr.size) continue;
                // Recorta o intervalo livre: sobra antes (alinhamento) e depois.
                const Range before{fr.offset, start - fr.offset};
                const Range after{end, fr.offset + fr.size - end};
                b.free.erase(b.free.begin() + static_cast<std::ptrdiff_t>(r));
                if (after.size) b.free.insert(b.free.begin() + static_cast<std::ptrdiff_t>(r), after);
                if (before.size) b.free.insert(b.free.begin() + static_cast<std::ptrdiff_t>(r), before);
                // 32 bits: bloco aberto por uma imagem (sem mapa) servindo agora
                // um buffer que escreve pela CPU — mapeia o bloco uma vez, aqui.
                if (mapNow && !b.mapped && !address_space::map_gpu_memory_eagerly())
                    (void)vkMapMemory(device_, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped);
                out.memory = b.memory;
                out.offset = start;
                out.size = end - start;
                out.memoryType = type;
                out.block = bi;
                out.mapped = b.mapped ? static_cast<u8*>(b.mapped) + start : nullptr;
                b.used += out.size;
                used_ += out.size;
                ++allocations_;
                return out;
            }
        }
        // Nenhum bloco coube: cria um novo e tenta de novo.
        Block nb;
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.allocationSize = blockSize;
        info.memoryTypeIndex = type;
        if (vkAllocateMemory(device_, &info, nullptr, &nb.memory) != VK_SUCCESS) {
            AUREA_LOG_ERROR("memoria: bloco de %llu MB falhou",
                            static_cast<unsigned long long>(blockSize >> 20));
            return out;
        }
        if (mapNow) (void)vkMapMemory(device_, nb.memory, 0, VK_WHOLE_SIZE, 0, &nb.mapped);
        nb.size = blockSize;
        nb.sizeClass = tierSize;
        nb.memoryType = type;
        nb.free.push_back(Range{0, blockSize});
        auto empty = std::find_if(blocks_.begin(), blocks_.end(), [](const Block& b) { return !b.memory; });
        if (empty != blocks_.end()) *empty = std::move(nb);
        else blocks_.push_back(std::move(nb));
        reserved_ += blockSize;
    }
    return out;
}

void MemoryAllocator::free(const Allocation& a) noexcept {
    if (!a.valid()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (allocations_) --allocations_;
    used_ -= std::min<u64>(used_, a.size);
    if (a.block == kInvalidIndex) {
        if (a.mapped) vkUnmapMemory(device_, a.memory);
        vkFreeMemory(device_, a.memory, nullptr);
        reserved_ -= std::min<u64>(reserved_, a.size);
        return;
    }
    if (a.block >= blocks_.size()) return;
    Block& b = blocks_[a.block];
    b.used -= std::min(b.used, a.size);
    // Insere ordenado e coalesce com os vizinhos.
    auto it = std::lower_bound(b.free.begin(), b.free.end(), a.offset,
                               [](const Range& r, VkDeviceSize off) { return r.offset < off; });
    it = b.free.insert(it, Range{a.offset, a.size});
    if (it + 1 != b.free.end() && it->offset + it->size == (it + 1)->offset) {
        it->size += (it + 1)->size;
        b.free.erase(it + 1);
    }
    if (it != b.free.begin() && (it - 1)->offset + (it - 1)->size == it->offset) {
        (it - 1)->size += it->size;
        b.free.erase(it);
    }
}

void MemoryAllocator::trim_empty_blocks() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Block& b : blocks_) {
        if (!b.memory || b.used) continue;
        if (b.mapped) vkUnmapMemory(device_, b.memory);
        vkFreeMemory(device_, b.memory, nullptr);
        reserved_ -= std::min<u64>(reserved_, b.size);
        b = Block{};
    }
}

void MemoryAllocator::log_memory_breakdown() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    struct Type { u64 reserved = 0; u64 used = 0; u64 largestFree = 0; u32 blocks = 0; };
    Type types[VK_MAX_MEMORY_TYPES]{};
    u64 blockReserved = 0, blockUsed = 0;
    u32 printed = 0, live = 0;
    for (u32 i = 0; i < blocks_.size(); ++i) {
        const Block& block = blocks_[i];
        if (!block.memory) continue;
        ++live;
        blockReserved += block.size; blockUsed += block.used;
        auto& type = types[block.memoryType];
        type.reserved += block.size; type.used += block.used; ++type.blocks;
        u64 largest = 0;
        for (const auto& range : block.free) largest = std::max<u64>(largest, range.size);
        type.largestFree = std::max(type.largestFree, largest);
        if (printed++ < 24) AUREA_LOG_WARN("GPU block: index=%u type=%u reserved=%llu used=%llu largest_free=%llu ranges=%zu size_class=%llu",
            i, block.memoryType, static_cast<unsigned long long>(block.size),
            static_cast<unsigned long long>(block.used), static_cast<unsigned long long>(largest), block.free.size(),
            static_cast<unsigned long long>(block.sizeClass));
    }
    AUREA_LOG_WARN("GPU backing: reserved=%llu used=%llu block_reserved=%llu block_used=%llu dedicated_reserved=%llu dedicated_used=%llu live_blocks=%u allocations=%u",
        static_cast<unsigned long long>(reserved_), static_cast<unsigned long long>(used_),
        static_cast<unsigned long long>(blockReserved), static_cast<unsigned long long>(blockUsed),
        static_cast<unsigned long long>(reserved_ - std::min(reserved_, blockReserved)),
        static_cast<unsigned long long>(used_ - std::min(used_, blockUsed)), live, allocations_);
    for (u32 i = 0; i < props_.memoryTypeCount; ++i) if (types[i].blocks) {
        const auto& type = types[i];
        AUREA_LOG_WARN("GPU memory type: type=%u heap=%u flags=%u reserved=%llu used=%llu largest_free=%llu blocks=%u",
            i, props_.memoryTypes[i].heapIndex, props_.memoryTypes[i].propertyFlags,
            static_cast<unsigned long long>(type.reserved), static_cast<unsigned long long>(type.used),
            static_cast<unsigned long long>(type.largestFree), type.blocks);
    }
}

// =============================================================================
// HostRing
// =============================================================================
void HostRing::initialize(Backend* backend, VkDeviceSize capacity, VkBufferUsageFlags usage,
                          const char* name) noexcept {
    backend_ = backend;
    usage_ = usage;
    name_ = name;
    initialCapacity_ = capacity;
    alignedDemand_ = peakAlignedDemand_ = 0;
    (void)add_chunk(capacity);
}

bool HostRing::add_chunk(VkDeviceSize size) noexcept {
    Chunk c;
    c.size = size;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage_;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(backend_->device(), &info, nullptr, &c.buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(backend_->device(), c.buffer, &req);
    c.alloc = backend_->allocator().allocate(req,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0, false, name_);
    if (!c.alloc.valid() || !c.alloc.mapped
        || vkBindBufferMemory(backend_->device(), c.buffer, c.alloc.memory, c.alloc.offset) != VK_SUCCESS) {
        vkDestroyBuffer(backend_->device(), c.buffer, nullptr);
        backend_->allocator().free(c.alloc);
        return false;
    }
    chunks_.push_back(c);
    return true;
}

void HostRing::shutdown() noexcept {
    if (!backend_) return;
    for (Chunk& c : chunks_) {
        vkDestroyBuffer(backend_->device(), c.buffer, nullptr);
        backend_->allocator().free(c.alloc);
    }
    chunks_.clear();
    offset_ = used_ = alignedDemand_ = 0;
}

void HostRing::reset() noexcept {
    // Called only after the owning frame's fence. Consolidate actual aligned
    // demand; summing geometric chunk capacities promoted unused tails into
    // permanent backing (4 + 8 MiB became 15 MiB for an 8 MiB workload).
    if (chunks_.size() > 1) {
        const VkDeviceSize want = std::max(initialCapacity_, peakAlignedDemand_);
        const VkDeviceSize extra = std::min(want / 4, std::numeric_limits<VkDeviceSize>::max() - want);
        shutdown();
        (void)add_chunk(want + extra);
    }
    offset_ = 0;
    used_ = alignedDemand_ = 0;
}

bool HostRing::allocate(VkDeviceSize size, VkDeviceSize align, VkBuffer& outBuffer,
                        VkDeviceSize& outOffset, void*& outPtr) noexcept {
    const VkDeviceSize alignment = std::max<VkDeviceSize>(1, align);
    const VkDeviceSize maximum = std::numeric_limits<VkDeviceSize>::max();
    if (alignedDemand_ > maximum - (alignment - 1)) return false;
    const VkDeviceSize virtualStart = align_up(alignedDemand_, alignment);
    if (size > maximum - virtualStart) return false;
    // A failed consolidation is recoverable on the next allocation attempt.
    if (chunks_.empty() && (!backend_ || size > maximum - alignment
            || !add_chunk(std::max(initialCapacity_, size + alignment)))) return false;
    Chunk* c = &chunks_.back();
    if (offset_ > maximum - (alignment - 1)) return false;
    VkDeviceSize start = align_up(offset_, alignment);
    if (start > c->size || size > c->size - start) {
        if (size > maximum - alignment) return false;
        const VkDeviceSize doubled = c->size <= maximum / 2 ? c->size * 2 : maximum;
        if (!add_chunk(std::max(size + alignment, doubled))) return false;
        c = &chunks_.back();
        offset_ = 0;
        start = 0;
    }
    outBuffer = c->buffer;
    outOffset = start;
    outPtr = static_cast<u8*>(c->alloc.mapped) + start;
    offset_ = start + size;
    used_ += size;
    alignedDemand_ = virtualStart + size;
    peakAlignedDemand_ = std::max(peakAlignedDemand_, alignedDemand_);
    return true;
}

HostRing::Stats HostRing::stats() const noexcept {
    Stats result;
    result.payloadUsed = used_; result.alignedDemand = alignedDemand_;
    result.peakAlignedDemand = peakAlignedDemand_; result.chunks = static_cast<u32>(chunks_.size());
    for (const auto& chunk : chunks_) { result.capacity += chunk.size; result.backing += chunk.alloc.size; }
    return result;
}

void Backend::log_memory_breakdown() noexcept {
    allocator_.log_memory_breakdown();
    // Failure-only attribution from existing live resource pools. No resource
    // names are retained (many debugName pointers have temporary lifetimes),
    // no allocation registry is added, and this grants no admission credit.
    u32 suballocatedTextures = 0, suballocatedBuffers = 0, dedicatedTextures = 0, dedicatedBuffers = 0;
    textures_.for_each([&](u64 handle, const Texture& texture) {
        const auto& a = texture.alloc;
        if (!a.valid()) return;
        const bool dedicated = a.block == kInvalidIndex;
        if (dedicated) ++dedicatedTextures; else ++suballocatedTextures;
        AUREA_LOG_WARN("GPU live texture: handle=%llu block=%u type=%u offset=%llu allocation=%llu width=%u height=%u format=%u samples=%u mips=%u layers=%u sampled=%u render_target=%u storage=%u transient=%u last_frame=%llu dedicated=%u",
            static_cast<unsigned long long>(handle), a.block, a.memoryType,
            static_cast<unsigned long long>(a.offset), static_cast<unsigned long long>(a.size),
            texture.desc.width, texture.desc.height, static_cast<u32>(texture.desc.format),
            texture.desc.sampleCount, texture.desc.mipLevels, texture.desc.layers,
            texture.desc.sampled ? 1u : 0u, texture.desc.renderTarget ? 1u : 0u,
            texture.desc.storage ? 1u : 0u, texture.desc.transient ? 1u : 0u,
            static_cast<unsigned long long>(texture.lastUsedFrame), dedicated ? 1u : 0u);
    });
    buffers_.for_each([&](u64 handle, const Buffer& buffer) {
        const auto& a = buffer.alloc;
        if (!a.valid()) return;
        const bool dedicated = a.block == kInvalidIndex;
        if (dedicated) ++dedicatedBuffers; else ++suballocatedBuffers;
        AUREA_LOG_WARN("GPU live buffer: handle=%llu block=%u type=%u offset=%llu allocation=%llu payload=%llu usage=%u access=%u dedicated=%u",
            static_cast<unsigned long long>(handle), a.block, a.memoryType,
            static_cast<unsigned long long>(a.offset), static_cast<unsigned long long>(a.size),
            static_cast<unsigned long long>(buffer.desc.bytes), static_cast<u32>(buffer.desc.usage),
            static_cast<u32>(buffer.desc.access), dedicated ? 1u : 0u);
    });
    AUREA_LOG_WARN("GPU live suballocations: textures=%u buffers=%u", suballocatedTextures, suballocatedBuffers);
    AUREA_LOG_WARN("GPU live dedicated allocations: textures=%u buffers=%u", dedicatedTextures, dedicatedBuffers);
    for (u32 i = 0; i < framesInFlight_; ++i) {
        const auto& frame = frames_[i];
        AUREA_LOG_WARN("GPU retirement: slot=%u frame=%llu submitted=%u open=%u deferred=%zu external_acquired=%zu",
            i, static_cast<unsigned long long>(frame.frameNumber), frame.submitted ? 1u : 0u,
            &frame == current_ ? 1u : 0u, frame.deferred.size(), frame.externalAcquired.size());
        for (const auto& item : {std::pair<const char*, const HostRing*>{"uniforms", &frame.uniforms},
                                 std::pair<const char*, const HostRing*>{"staging", &frame.staging}}) {
            const auto ring = item.second->stats();
            AUREA_LOG_WARN("GPU scratch: slot=%u frame=%llu submitted=%u open=%u kind=%s capacity=%llu backing=%llu payload=%llu aligned_demand=%llu peak_aligned_demand=%llu chunks=%u",
                i, static_cast<unsigned long long>(frame.frameNumber), frame.submitted ? 1u : 0u,
                &frame == current_ ? 1u : 0u, item.first,
                static_cast<unsigned long long>(ring.capacity), static_cast<unsigned long long>(ring.backing),
                static_cast<unsigned long long>(ring.payloadUsed), static_cast<unsigned long long>(ring.alignedDemand),
                static_cast<unsigned long long>(ring.peakAlignedDemand), ring.chunks);
        }
    }
    usize immediateDeferred = 0;
    for (const auto& item : pendingImmediate_) immediateDeferred += item.deferred.size();
    AUREA_LOG_WARN("GPU immediate retirement: submissions=%zu deferred=%zu", pendingImmediate_.size(), immediateDeferred);
}

} // namespace aurea::vk

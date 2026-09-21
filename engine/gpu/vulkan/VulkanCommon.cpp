#include "VulkanBackend.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>

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
    }
    return VK_FORMAT_R8G8B8A8_UNORM;
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
constexpr VkDeviceSize kDeviceBlock = 64ull * 1024 * 1024;
constexpr VkDeviceSize kHostBlock   = 16ull * 1024 * 1024;

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
                                     const char* debugName) noexcept {
    Allocation out;
    const u32 type = find_type(req.memoryTypeBits, required, preferred);
    if (type == kInvalidIndex) {
        AUREA_LOG_ERROR("memoria: nenhum tipo compativel para '%s'", debugName ? debugName : "?");
        return out;
    }
    const VkMemoryPropertyFlags flags = props_.memoryTypes[type].propertyFlags;
    const bool hostVisible = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    const VkDeviceSize blockSize = hostVisible ? kHostBlock : kDeviceBlock;
    const VkDeviceSize align = std::max(req.alignment, granularity_);

    std::lock_guard<std::mutex> lock(mutex_);

    // Grande demais para bloco: alocação dedicada (texturas 4K de vídeo,
    // anéis grandes). Subalocar isso fragmentaria o bloco inteiro.
    if (dedicated || req.size > blockSize / 2) {
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.allocationSize = req.size;
        info.memoryTypeIndex = type;
        if (vkAllocateMemory(device_, &info, nullptr, &out.memory) != VK_SUCCESS) {
            AUREA_LOG_ERROR("memoria: dedicada de %llu bytes falhou (%s)",
                            static_cast<unsigned long long>(req.size), debugName ? debugName : "?");
            out.memory = VK_NULL_HANDLE;
            return out;
        }
        if (hostVisible) (void)vkMapMemory(device_, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped);
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
            if (b.memoryType != type) continue;
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
        if (hostVisible) (void)vkMapMemory(device_, nb.memory, 0, VK_WHOLE_SIZE, 0, &nb.mapped);
        nb.size = blockSize;
        nb.memoryType = type;
        nb.free.push_back(Range{0, blockSize});
        blocks_.push_back(std::move(nb));
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

// =============================================================================
// HostRing
// =============================================================================
void HostRing::initialize(Backend* backend, VkDeviceSize capacity, VkBufferUsageFlags usage,
                          const char* name) noexcept {
    backend_ = backend;
    usage_ = usage;
    name_ = name;
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
    offset_ = used_ = 0;
}

void HostRing::reset() noexcept {
    // Se o frame precisou de mais de um bloco, troca todos por um só do tamanho
    // do pico: em regime, o anel não cresce mais e não há alocação por frame.
    if (chunks_.size() > 1) {
        VkDeviceSize total = 0;
        for (const Chunk& c : chunks_) total += c.size;
        const VkDeviceSize want = std::max(total, peak_);
        shutdown();
        (void)add_chunk(want + want / 4);
    }
    offset_ = 0;
    used_ = 0;
}

bool HostRing::allocate(VkDeviceSize size, VkDeviceSize align, VkBuffer& outBuffer,
                        VkDeviceSize& outOffset, void*& outPtr) noexcept {
    if (chunks_.empty()) return false;
    Chunk* c = &chunks_.back();
    VkDeviceSize start = align_up(offset_, align ? align : 1);
    if (start + size > c->size) {
        if (!add_chunk(std::max(size + align, c->size * 2))) return false;
        c = &chunks_.back();
        offset_ = 0;
        start = 0;
    }
    outBuffer = c->buffer;
    outOffset = start;
    outPtr = static_cast<u8*>(c->alloc.mapped) + start;
    offset_ = start + size;
    used_ += size;
    peak_ = std::max(peak_, used_);
    return true;
}

} // namespace aurea::vk

// =============================================================================
//  Instância, dispositivo, capacidades, contextos de frame e ciclo de vida.
// =============================================================================
#include "VulkanBackend.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace aurea::vk {
namespace {

bool has_extension(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const VkExtensionProperties& e : list) {
        if (std::strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    // Erro de validação é bug. Aviso de sincronização também: não se ignora.
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        AUREA_LOG_ERROR("validacao: %s", data && data->pMessage ? data->pMessage : "?");
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        AUREA_LOG_WARN("validacao: %s", data && data->pMessage ? data->pMessage : "?");
    }
    return VK_FALSE;
}

} // namespace

Backend::~Backend() { shutdown(); }

bool Backend::note_device_lost(VkResult r) noexcept {
    if (r == VK_ERROR_DEVICE_LOST) {
        if (!deviceLost_) AUREA_LOG_ERROR("vulkan: dispositivo perdido");
        deviceLost_ = true;
        return true;
    }
    return false;
}

// =============================================================================
// Ciclo de vida
// =============================================================================
Status Backend::initialize(const BackendConfig& config) noexcept {
    if (initialized_) return OkStatus;
    config_ = config;
    framesInFlight_ = std::clamp<u32>(config.framesInFlight, 1, 3);
    deviceLost_ = false;

    if (!load_library()) return Status{Errc::NotSupported, "biblioteca Vulkan ausente"};
    if (const Status s = create_instance(config.enableValidation); !s.ok()) return s;
    if (const Status s = pick_device(); !s.ok()) return s;
    if (const Status s = create_device(); !s.ok()) return s;

    allocator_.initialize(physical_, device_);
    fill_capabilities();
    load_pipeline_cache();

    if (const Status s = create_frames(); !s.ok()) return s;
    if (const Status s = create_dummies(); !s.ok()) return s;

    initialized_ = true;
    AUREA_LOG_INFO("vulkan: %s", caps_.summary().c_str());
    return OkStatus;
}

void Backend::shutdown() noexcept {
    if (!device_ && !instance_) return;
    if (device_) vkDeviceWaitIdle(device_);

    for (u32 i = 0; i < 3; ++i) run_deferred(frames_[i]);
    destroy_swapchain();
    if (surface_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    if (device_) {
        save_pipeline_cache();
        textures_.for_each([this](u64, Texture& t) { destroy_texture_now(t); });
        textures_.clear();
        buffers_.for_each([this](u64, Buffer& b) { destroy_buffer_now(b); });
        buffers_.clear();
        pipelines_.for_each([this](u64, PipelineObject& p) { vkDestroyPipeline(device_, p.pipeline, nullptr); });
        pipelines_.clear();
        shaders_.for_each([this](u64, ShaderModule& s) { vkDestroyShaderModule(device_, s.module, nullptr); });
        shaders_.clear();
        samplers_.for_each([this](u64, SamplerObject& s) {
            vkDestroySampler(device_, s.sampler, nullptr);
            if (s.conversion) vkDestroySamplerYcbcrConversion(device_, s.conversion, nullptr);
        });
        samplers_.clear();
        for (auto& [k, rp] : renderPasses_) vkDestroyRenderPass(device_, rp, nullptr);
        renderPasses_.clear();
        for (auto& [k, l] : layouts_) vkDestroyPipelineLayout(device_, l, nullptr);
        layouts_.clear();
        for (auto& [k, l] : setLayouts_) vkDestroyDescriptorSetLayout(device_, l, nullptr);
        setLayouts_.clear();
        ycbcrByFormat_.clear();
        importedByBuffer_.clear();
        if (defaultSampler_) vkDestroySampler(device_, defaultSampler_, nullptr);
        defaultSampler_ = VK_NULL_HANDLE;
        destroy_frames();
        if (pipelineCache_) vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
        pipelineCache_ = VK_NULL_HANDLE;
        allocator_.shutdown();
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (messenger_) {
        vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
        messenger_ = VK_NULL_HANDLE;
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    current_ = lastSubmitted_ = nullptr;
    initialized_ = false;
}

// =============================================================================
// Instância
// =============================================================================
Status Backend::create_instance(bool validation) noexcept {
    u32 version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&version);
    if (version < VK_API_VERSION_1_1) {
        // YCbCr (zero-copy de vídeo), memória externa e maintenance1 são do
        // 1.1. Sem eles o pipeline de vídeo do Aurea não existe.
        return Status{Errc::UnsupportedFeature, "Vulkan 1.1 necessario"};
    }

    u32 count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());

    std::vector<const char*> enabled;
    if (has_extension(exts, VK_KHR_SURFACE_EXTENSION_NAME)) {
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
        if (has_extension(exts, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) {
            enabled.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
            enabled.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
            hasSurfaceExt_ = true;
        }
#endif
    }
    debugUtils_ = has_extension(exts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debugUtils_) enabled.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char*> layers;
    if (validation) {
        u32 lc = 0;
        vkEnumerateInstanceLayerProperties(&lc, nullptr);
        std::vector<VkLayerProperties> props(lc);
        vkEnumerateInstanceLayerProperties(&lc, props.data());
        bool found = false;
        for (const VkLayerProperties& l : props) {
            if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) found = true;
        }
        if (found) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            caps_.validationEnabled = true;
        } else {
            AUREA_LOG_WARN("vulkan: camada de validacao pedida mas nao instalada");
        }
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Aurea Editor";
    app.applicationVersion = VK_MAKE_VERSION(2, 0, 0);
    app.pEngineName = "Aurea Engine";
    app.engineVersion = VK_MAKE_VERSION(2, 0, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<u32>(enabled.size());
    info.ppEnabledExtensionNames = enabled.data();
    info.enabledLayerCount = static_cast<u32>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    if (const Status s = check(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance"); !s.ok()) return s;
    load_instance(instance_);

    if (debugUtils_ && caps_.validationEnabled && vkCreateDebugUtilsMessengerEXT) {
        VkDebugUtilsMessengerCreateInfoEXT m{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        m.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        m.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                      | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        m.pfnUserCallback = &debug_callback;
        (void)vkCreateDebugUtilsMessengerEXT(instance_, &m, nullptr, &messenger_);
    }
    return OkStatus;
}

// =============================================================================
// Dispositivo
// =============================================================================
Status Backend::pick_device() noexcept {
    u32 count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) return Status{Errc::NotSupported, "nenhuma GPU Vulkan"};
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    // Preferência: discreta > integrada > outras, sempre com fila gráfica+compute
    // e Vulkan 1.1. No celular só há uma; no host de testes, pega a dedicada.
    i32 bestScore = -1;
    for (VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.apiVersion < VK_API_VERSION_1_1) continue;
        u32 qc = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qc);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, qs.data());
        u32 family = kInvalidIndex;
        for (u32 i = 0; i < qc; ++i) {
            if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (qs[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                family = i;
                break;
            }
        }
        if (family == kInvalidIndex) continue;
        i32 score = 1;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 4;
        else if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 3;
        else if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) score = 2;
        if (score > bestScore) {
            bestScore = score;
            physical_ = d;
            graphicsFamily_ = family;
            apiVersion_ = p.apiVersion;
        }
    }
    if (!physical_) return Status{Errc::NotSupported, "nenhuma GPU com Vulkan 1.1 e fila grafica"};
    return OkStatus;
}

Status Backend::create_device() noexcept {
    u32 count = 0;
    vkEnumerateDeviceExtensionProperties(physical_, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(physical_, nullptr, &count, exts.data());

    std::vector<const char*> enabled;
    if (hasSurfaceExt_ && has_extension(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        enabled.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    // Zero-copy: o AHardwareBuffer do MediaCodec vira VkImage. Precisa das
    // duas extensões — memória externa de AHB e posse vinda de fila
    // estrangeira (o decoder não é uma fila Vulkan).
    if (has_extension(exts, VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME)) {
        enabled.push_back(VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);
        hasAhb_ = true;
        if (has_extension(exts, VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME)) {
            enabled.push_back(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
            hasForeignQueue_ = true;
        }
    }
#endif

    VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES};
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &ycbcr;
    vkGetPhysicalDeviceFeatures2(physical_, &features2);
    hasYcbcr_ = ycbcr.samplerYcbcrConversion == VK_TRUE;

    VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcrEnable{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES};
    ycbcrEnable.samplerYcbcrConversion = hasYcbcr_ ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceFeatures2 enable{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    enable.pNext = &ycbcrEnable;
    // Só o que o motor usa. Ligar recurso "por via das dúvidas" custa em alguns
    // drivers móveis (e alguns recusam o dispositivo inteiro).
    enable.features.samplerAnisotropy = features2.features.samplerAnisotropy;
    enable.features.fragmentStoresAndAtomics = features2.features.fragmentStoresAndAtomics;
    // Texturas comprimidas (KTX2/Basis transcodificado para o formato nativo).
    enable.features.textureCompressionASTC_LDR = features2.features.textureCompressionASTC_LDR;
    enable.features.textureCompressionETC2 = features2.features.textureCompressionETC2;
    enable.features.textureCompressionBC = features2.features.textureCompressionBC;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = graphicsFamily_;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;

    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &enable;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &q;
    info.enabledExtensionCount = static_cast<u32>(enabled.size());
    info.ppEnabledExtensionNames = enabled.data();
    if (const Status s = check(vkCreateDevice(physical_, &info, nullptr, &device_), "vkCreateDevice"); !s.ok()) return s;
    load_device(device_);
    vkGetDeviceQueue(device_, graphicsFamily_, 0, &queue_);

    caps_.extensions.clear();
    for (const char* e : enabled) caps_.extensions.emplace_back(e);
    return OkStatus;
}

void Backend::fill_capabilities() noexcept {
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(physical_, &p);
    VkPhysicalDeviceFeatures f{};
    vkGetPhysicalDeviceFeatures(physical_, &f);

    VkPhysicalDeviceShaderFloat16Int8Features f16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
    VkPhysicalDevice16BitStorageFeatures s16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    f16.pNext = &s16;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f16;
    vkGetPhysicalDeviceFeatures2(physical_, &f2);

    caps_.apiName = "Vulkan";
    caps_.apiMajor = VK_VERSION_MAJOR(p.apiVersion);
    caps_.apiMinor = VK_VERSION_MINOR(p.apiVersion);
    caps_.apiPatch = VK_VERSION_PATCH(p.apiVersion);
    caps_.deviceName = p.deviceName;
    char drv[64];
    std::snprintf(drv, sizeof(drv), "driver 0x%08x", p.driverVersion);
    caps_.driverInfo = drv;
    caps_.vendorId = p.vendorID;
    caps_.deviceId = p.deviceID;
    switch (p.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: caps_.deviceType = GpuDeviceType::Integrated; break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   caps_.deviceType = GpuDeviceType::Discrete; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    caps_.deviceType = GpuDeviceType::Virtual; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            caps_.deviceType = GpuDeviceType::Cpu; break;
        default: break;
    }
    caps_.fp16Arithmetic = f16.shaderFloat16 == VK_TRUE;
    caps_.int16Arithmetic = f.shaderInt16 == VK_TRUE;
    caps_.fp16Storage = s16.storageBuffer16BitAccess == VK_TRUE;

    const VkPhysicalDeviceLimits& l = p.limits;
    caps_.maxTexture2D = l.maxImageDimension2D;
    caps_.maxComputeWorkGroupInvocations = l.maxComputeWorkGroupInvocations;
    for (int i = 0; i < 3; ++i) caps_.maxComputeWorkGroupSize[i] = l.maxComputeWorkGroupSize[i];
    caps_.maxComputeSharedMemoryBytes = l.maxComputeSharedMemorySize;
    caps_.maxPushConstantBytes = l.maxPushConstantsSize;
    caps_.maxUniformBufferRange = l.maxUniformBufferRange;
    caps_.maxBoundDescriptorSets = l.maxBoundDescriptorSets;
    caps_.maxPerStageSampledImages = l.maxPerStageDescriptorSampledImages;
    caps_.maxPerStageStorageImages = l.maxPerStageDescriptorStorageImages;
    caps_.maxColorAttachments = l.maxColorAttachments;
    caps_.minUniformBufferOffsetAlignment = static_cast<u32>(std::max<VkDeviceSize>(16, l.minUniformBufferOffsetAlignment));
    caps_.colorSampleCountMask = l.framebufferColorSampleCounts;

    // Timestamps: a fila precisa ter bits válidos e o período ser conhecido.
    u32 qc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &qc, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qc);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &qc, qs.data());
    const u32 validBits = graphicsFamily_ < qc ? qs[graphicsFamily_].timestampValidBits : 0;
    caps_.timestampQueries = validBits > 0 && l.timestampPeriod > 0.0f
                          && (l.timestampComputeAndGraphics == VK_TRUE || validBits > 0);
    caps_.timestampPeriodNs = l.timestampPeriod;
    timestampPeriod_ = l.timestampPeriod;
    timestampMask_ = validBits >= 64 ? ~0ull : ((1ull << validBits) - 1ull);
    timersEnabled_ = caps_.timestampQueries && config_.enableGpuTimers;

    auto fmt = [&](VkFormat vf, VkFormatFeatureFlags want) {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(physical_, vf, &fp);
        return (fp.optimalTilingFeatures & want) == want;
    };
    caps_.rgba16fRenderable = fmt(VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
    caps_.rgba16fFilterable = fmt(VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
    caps_.rgba16fStorage = fmt(VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
    caps_.r16UnormSampled = fmt(VK_FORMAT_R16_UNORM, VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
    caps_.maxSamplerAnisotropy = f.samplerAnisotropy ? std::max(1.0f, l.maxSamplerAnisotropy) : 1.0f;
    caps_.depth32fAttachment = fmt(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    caps_.depth24Attachment = fmt(VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    caps_.depth32fSampled = fmt(VK_FORMAT_D32_SFLOAT, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    caps_.textureCompressionASTC = f.textureCompressionASTC_LDR == VK_TRUE;
    caps_.textureCompressionETC2 = f.textureCompressionETC2 == VK_TRUE;
    caps_.textureCompressionBC = f.textureCompressionBC == VK_TRUE;
    caps_.maxVertexInputAttributes = l.maxVertexInputAttributes;

    caps_.samplerYcbcrConversion = hasYcbcr_;
    caps_.externalMemoryHardwareBuffer = hasAhb_;
    caps_.queueFamilyForeign = hasForeignQueue_;
    caps_.externalSemaphoreFd = false;

    const VkPhysicalDeviceMemoryProperties& mp = allocator_.properties();
    caps_.heaps.clear();
    caps_.deviceLocalBytes = caps_.hostVisibleBytes = 0;
    for (u32 i = 0; i < mp.memoryHeapCount; ++i) {
        GpuMemoryHeap h;
        h.bytes = mp.memoryHeaps[i].size;
        h.deviceLocal = (mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
        caps_.heaps.push_back(h);
        if (h.deviceLocal) caps_.deviceLocalBytes += h.bytes;
    }
    for (u32 i = 0; i < mp.memoryTypeCount; ++i) {
        if (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            caps_.hostVisibleBytes = std::max<u64>(caps_.hostVisibleBytes, mp.memoryHeaps[mp.memoryTypes[i].heapIndex].size);
        }
    }
    caps_.unifiedMemory = allocator_.unified();
}

// =============================================================================
// Cache de pipeline persistido
// =============================================================================
void Backend::load_pipeline_cache() noexcept {
    std::vector<u8> data;
    if (config_.cacheDirectory && *config_.cacheDirectory) {
        cachePath_ = std::string(config_.cacheDirectory) + "/aurea_pipeline_cache.bin";
        if (std::FILE* f = std::fopen(cachePath_.c_str(), "rb")) {
            std::fseek(f, 0, SEEK_END);
            const long size = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (size > 0 && size < 64 * 1024 * 1024) {
                data.resize(static_cast<usize>(size));
                if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
            }
            std::fclose(f);
        }
    }
    // O driver valida o cabeçalho (fornecedor, dispositivo, UUID). Cache de
    // outro driver é ignorado por ele — não há o que conferir aqui.
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = data.size();
    info.pInitialData = data.empty() ? nullptr : data.data();
    if (vkCreatePipelineCache(device_, &info, nullptr, &pipelineCache_) != VK_SUCCESS) {
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        (void)vkCreatePipelineCache(device_, &info, nullptr, &pipelineCache_);
    }
    if (!data.empty()) AUREA_LOG_INFO("vulkan: cache de pipeline carregado (%zu KB)", data.size() / 1024);
}

void Backend::save_pipeline_cache() noexcept {
    if (!device_ || !pipelineCache_ || cachePath_.empty()) return;
    usize size = 0;
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, nullptr) != VK_SUCCESS || size == 0) return;
    std::vector<u8> data(size);
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, data.data()) != VK_SUCCESS) return;
    const std::string tmp = cachePath_ + ".tmp";
    if (std::FILE* f = std::fopen(tmp.c_str(), "wb")) {
        const bool ok = std::fwrite(data.data(), 1, size, f) == size;
        std::fclose(f);
        // Troca atômica: um cache pela metade (app morto no meio) nunca
        // substitui o bom.
        if (ok) {
            std::remove(cachePath_.c_str());
            (void)std::rename(tmp.c_str(), cachePath_.c_str());
        }
    }
}

// =============================================================================
// Contextos de frame
// =============================================================================
Status Backend::create_frames() noexcept {
    for (u32 i = 0; i < framesInFlight_; ++i) {
        FrameContext& f = frames_[i];
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pi.queueFamilyIndex = graphicsFamily_;
        if (const Status s = check(vkCreateCommandPool(device_, &pi, nullptr, &f.pool), "vkCreateCommandPool"); !s.ok()) return s;
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (const Status s = check(vkAllocateCommandBuffers(device_, &ai, &f.cmd), "vkAllocateCommandBuffers"); !s.ok()) return s;
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // o primeiro wait não pode travar
        if (const Status s = check(vkCreateFence(device_, &fi, nullptr, &f.fence), "vkCreateFence"); !s.ok()) return s;
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (const Status s = check(vkCreateSemaphore(device_, &si, nullptr, &f.acquired), "vkCreateSemaphore"); !s.ok()) return s;
        f.uniforms.initialize(this, 256 * 1024, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, "anel-uniforms");
        f.staging.initialize(this, 4 * 1024 * 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "anel-staging");
        if (timersEnabled_) {
            VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qi.queryCount = kMaxTimers * 2 + 2;
            if (vkCreateQueryPool(device_, &qi, nullptr, &f.queries) == VK_SUCCESS) f.queryCount = qi.queryCount;
        }
        f.timerLabels.reserve(kMaxTimers);
        f.deferred.reserve(64);
    }
    return OkStatus;
}

void Backend::destroy_frames() noexcept {
    for (FrameContext& f : frames_) {
        f.uniforms.shutdown();
        f.staging.shutdown();
        for (VkDescriptorPool p : f.descriptorPools) vkDestroyDescriptorPool(device_, p, nullptr);
        f.descriptorPools.clear();
        if (f.queries) vkDestroyQueryPool(device_, f.queries, nullptr);
        if (f.acquired) vkDestroySemaphore(device_, f.acquired, nullptr);
        if (f.fence) vkDestroyFence(device_, f.fence, nullptr);
        if (f.pool) vkDestroyCommandPool(device_, f.pool, nullptr);
        f = FrameContext{};
    }
}

// =============================================================================
// Recursos de reserva: o que um slot não amarrado enxerga. Todo slot declarado
// num shader precisa de descritor válido; a textura preta 1x1 e o buffer vazio
// garantem isso sem cada passe ter de pensar nos slots que não usa.
// =============================================================================
Status Backend::create_dummies() noexcept {
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    if (const Status s = check(vkCreateSampler(device_, &si, nullptr, &defaultSampler_), "vkCreateSampler"); !s.ok()) return s;

    TextureDesc d;
    d.width = d.height = 1;
    d.format = SurfaceFormat::RGBA8;
    d.sampled = true;
    d.transferDst = true;
    d.debugName = "reserva-amostrada";
    auto t = create_texture(d);
    if (!t.ok()) return t.status();
    dummyTexture_ = t->id;
    const u8 black[4] = {0, 0, 0, 0};
    if (const Status s = upload_texture(*t, black, 4); !s.ok()) return s;

    TextureDesc ds;
    ds.width = ds.height = 1;
    ds.format = SurfaceFormat::RGBA16F;
    ds.sampled = false;
    ds.storage = true;
    ds.debugName = "reserva-storage";
    auto st = create_texture(ds);
    if (!st.ok()) return st.status();
    dummyStorage_ = st->id;
    struct Ctx { Backend* b; u64 id; } ctx{this, dummyStorage_};
    if (const Status s = submit_immediate([](Backend& b, VkCommandBuffer cmd, void* p) {
            Texture* tex = b.texture(static_cast<Ctx*>(p)->id);
            if (tex) b.transition(cmd, *tex, ResourceState::StorageReadWrite, true);
        }, &ctx); !s.ok()) {
        return s;
    }

    BufferDesc bd;
    bd.bytes = 256;
    bd.usage = BufferUsage::Storage;
    bd.debugName = "reserva-buffer";
    auto b = create_buffer(bd);
    if (!b.ok()) return b.status();
    dummyBuffer_ = b->id;
    return OkStatus;
}

} // namespace aurea::vk

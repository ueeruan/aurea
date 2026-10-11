#include "VulkanLoader.hpp"
#include <mutex>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace aurea::vk {

#define AUREA_VK_DEFINE(name) PFN_##name name = nullptr;
AUREA_VK_GLOBAL_FUNCTIONS(AUREA_VK_DEFINE)
#undef AUREA_VK_DEFINE

PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

namespace {
#if defined(_WIN32)
HMODULE g_library = nullptr;
#else
void* g_library = nullptr;
#endif
} // namespace

bool load_library() noexcept {
    // Initialization only; immutable bootstrap dispatch remains alive for all owners.
    static std::mutex initialization;
    std::lock_guard<std::mutex> lock(initialization);
    static bool initialized = false;
    if (initialized) return true;
    if (!vkGetInstanceProcAddr) {
#if defined(_WIN32)
    g_library = LoadLibraryA("vulkan-1.dll");
    if (!g_library) return false;
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        reinterpret_cast<void*>(GetProcAddress(g_library, "vkGetInstanceProcAddr")));
#else
    g_library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_library) g_library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!g_library) return false;
    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(g_library, "vkGetInstanceProcAddr"));
#endif
    }
    if (!vkGetInstanceProcAddr) return false;
#define AUREA_VK_LOAD_GLOBAL(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(nullptr, #name));
    AUREA_VK_GLOBAL_FUNCTIONS(AUREA_VK_LOAD_GLOBAL)
#undef AUREA_VK_LOAD_GLOBAL
    initialized = vkCreateInstance && vkEnumerateInstanceExtensionProperties
        && vkEnumerateInstanceLayerProperties;
    return initialized;
}

void load_instance(VkInstance instance, InstanceDispatch& functions) noexcept {
#define AUREA_VK_LOAD_INSTANCE(name) functions.name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));
    AUREA_VK_INSTANCE_FUNCTIONS(AUREA_VK_LOAD_INSTANCE)
    AUREA_VK_PLATFORM_INSTANCE_FUNCTIONS(AUREA_VK_LOAD_INSTANCE)
#undef AUREA_VK_LOAD_INSTANCE
}

void load_device(const InstanceDispatch& instanceFunctions, VkDevice device, DeviceDispatch& functions) noexcept {
#define AUREA_VK_LOAD_DEVICE(name) functions.name = reinterpret_cast<PFN_##name>(instanceFunctions.vkGetDeviceProcAddr(device, #name));
    AUREA_VK_DEVICE_FUNCTIONS(AUREA_VK_LOAD_DEVICE)
    AUREA_VK_PLATFORM_DEVICE_FUNCTIONS(AUREA_VK_LOAD_DEVICE)
#undef AUREA_VK_LOAD_DEVICE
}

} // namespace aurea::vk

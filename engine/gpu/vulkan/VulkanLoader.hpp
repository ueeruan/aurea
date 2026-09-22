// =============================================================================
//  Aurea / gpu / vulkan / VulkanLoader.hpp
//
//  Carregamento dinâmico das funções do Vulkan.
//
//  Por que não linkar direto na libvulkan: o MESMO backend roda no Android e no
//  host de testes (Windows/Linux). No host não há biblioteca de importação sem
//  instalar o Vulkan SDK; carregando `vulkan-1.dll`/`libvulkan.so` em runtime,
//  basta o driver da GPU. E as funções de dispositivo vêm de
//  `vkGetDeviceProcAddr`, que pula o despacho do loader — mais rápido por
//  chamada, o que importa num backend que grava milhares de comandos por
//  segundo.
//
//  Os ponteiros vivem em `aurea::vk` com os NOMES oficiais: o código do backend
//  escreve `vkCmdDraw(...)` normalmente.
// =============================================================================
#pragma once

#if defined(__ANDROID__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
    #define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#ifndef VK_NO_PROTOTYPES
    #define VK_NO_PROTOTYPES 1
#endif
#include <vulkan/vulkan.h>

namespace aurea::vk {

#define AUREA_VK_GLOBAL_FUNCTIONS(X)            \
    X(vkCreateInstance)                         \
    X(vkEnumerateInstanceExtensionProperties)   \
    X(vkEnumerateInstanceLayerProperties)       \
    X(vkEnumerateInstanceVersion)

#define AUREA_VK_INSTANCE_FUNCTIONS(X)                  \
    X(vkDestroyInstance)                                \
    X(vkEnumeratePhysicalDevices)                       \
    X(vkGetPhysicalDeviceProperties)                    \
    X(vkGetPhysicalDeviceProperties2)                   \
    X(vkGetPhysicalDeviceFeatures)                      \
    X(vkGetPhysicalDeviceFeatures2)                     \
    X(vkGetPhysicalDeviceMemoryProperties)              \
    X(vkGetPhysicalDeviceQueueFamilyProperties)         \
    X(vkGetPhysicalDeviceFormatProperties)              \
    X(vkEnumerateDeviceExtensionProperties)             \
    X(vkCreateDevice)                                   \
    X(vkGetDeviceProcAddr)                              \
    X(vkDestroySurfaceKHR)                              \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)             \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)        \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)             \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)        \
    X(vkCreateDebugUtilsMessengerEXT)                   \
    X(vkDestroyDebugUtilsMessengerEXT)

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    #define AUREA_VK_PLATFORM_INSTANCE_FUNCTIONS(X) X(vkCreateAndroidSurfaceKHR)
    #define AUREA_VK_PLATFORM_DEVICE_FUNCTIONS(X) X(vkGetAndroidHardwareBufferPropertiesANDROID)
#else
    #define AUREA_VK_PLATFORM_INSTANCE_FUNCTIONS(X)
    #define AUREA_VK_PLATFORM_DEVICE_FUNCTIONS(X)
#endif

#define AUREA_VK_DEVICE_FUNCTIONS(X)            \
    X(vkDestroyDevice)                          \
    X(vkGetDeviceQueue)                         \
    X(vkDeviceWaitIdle)                         \
    X(vkQueueSubmit)                            \
    X(vkQueueWaitIdle)                          \
    X(vkQueuePresentKHR)                        \
    X(vkCreateSwapchainKHR)                     \
    X(vkDestroySwapchainKHR)                    \
    X(vkGetSwapchainImagesKHR)                  \
    X(vkAcquireNextImageKHR)                    \
    X(vkAllocateMemory)                         \
    X(vkFreeMemory)                             \
    X(vkMapMemory)                              \
    X(vkUnmapMemory)                            \
    X(vkFlushMappedMemoryRanges)                \
    X(vkInvalidateMappedMemoryRanges)           \
    X(vkBindBufferMemory)                       \
    X(vkBindImageMemory)                        \
    X(vkGetBufferMemoryRequirements)            \
    X(vkGetImageMemoryRequirements)             \
    X(vkGetImageMemoryRequirements2)            \
    X(vkCreateBuffer)                           \
    X(vkDestroyBuffer)                          \
    X(vkCreateImage)                            \
    X(vkDestroyImage)                           \
    X(vkCreateImageView)                        \
    X(vkDestroyImageView)                       \
    X(vkCreateSampler)                          \
    X(vkDestroySampler)                         \
    X(vkCreateSamplerYcbcrConversion)           \
    X(vkDestroySamplerYcbcrConversion)          \
    X(vkCreateShaderModule)                     \
    X(vkDestroyShaderModule)                    \
    X(vkCreatePipelineCache)                    \
    X(vkDestroyPipelineCache)                   \
    X(vkGetPipelineCacheData)                   \
    X(vkCreateGraphicsPipelines)                \
    X(vkCreateComputePipelines)                 \
    X(vkDestroyPipeline)                        \
    X(vkCreatePipelineLayout)                   \
    X(vkDestroyPipelineLayout)                  \
    X(vkCreateDescriptorSetLayout)              \
    X(vkDestroyDescriptorSetLayout)             \
    X(vkCreateDescriptorPool)                   \
    X(vkDestroyDescriptorPool)                  \
    X(vkResetDescriptorPool)                    \
    X(vkAllocateDescriptorSets)                 \
    X(vkUpdateDescriptorSets)                   \
    X(vkCreateRenderPass)                       \
    X(vkDestroyRenderPass)                      \
    X(vkCreateFramebuffer)                      \
    X(vkDestroyFramebuffer)                     \
    X(vkCreateCommandPool)                      \
    X(vkDestroyCommandPool)                     \
    X(vkResetCommandPool)                       \
    X(vkAllocateCommandBuffers)                 \
    X(vkFreeCommandBuffers)                     \
    X(vkBeginCommandBuffer)                     \
    X(vkEndCommandBuffer)                       \
    X(vkCreateFence)                            \
    X(vkDestroyFence)                           \
    X(vkResetFences)                            \
    X(vkWaitForFences)                          \
    X(vkCreateSemaphore)                        \
    X(vkDestroySemaphore)                       \
    X(vkCreateQueryPool)                        \
    X(vkDestroyQueryPool)                       \
    X(vkGetQueryPoolResults)                    \
    X(vkCmdResetQueryPool)                      \
    X(vkCmdWriteTimestamp)                      \
    X(vkCmdBeginRenderPass)                     \
    X(vkCmdEndRenderPass)                       \
    X(vkCmdBindPipeline)                        \
    X(vkCmdBindDescriptorSets)                  \
    X(vkCmdPushConstants)                       \
    X(vkCmdSetViewport)                         \
    X(vkCmdSetScissor)                          \
    X(vkCmdDraw)                                \
    X(vkCmdDrawIndexed)                         \
    X(vkCmdBindVertexBuffers)                   \
    X(vkCmdBindIndexBuffer)                     \
    X(vkCmdBlitImage)                           \
    X(vkCmdDispatch)                            \
    X(vkCmdPipelineBarrier)                     \
    X(vkCmdCopyBufferToImage)                   \
    X(vkCmdCopyImageToBuffer)                   \
    X(vkCmdCopyImage)                           \
    X(vkCmdCopyBuffer)                          \
    X(vkCmdBeginDebugUtilsLabelEXT)             \
    X(vkCmdEndDebugUtilsLabelEXT)               \
    X(vkSetDebugUtilsObjectNameEXT)

#define AUREA_VK_DECLARE(name) extern PFN_##name name;
AUREA_VK_GLOBAL_FUNCTIONS(AUREA_VK_DECLARE)
AUREA_VK_INSTANCE_FUNCTIONS(AUREA_VK_DECLARE)
AUREA_VK_PLATFORM_INSTANCE_FUNCTIONS(AUREA_VK_DECLARE)
AUREA_VK_DEVICE_FUNCTIONS(AUREA_VK_DECLARE)
AUREA_VK_PLATFORM_DEVICE_FUNCTIONS(AUREA_VK_DECLARE)
#undef AUREA_VK_DECLARE

extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

/// Abre a biblioteca do sistema. `false` = não há Vulkan neste aparelho/host.
[[nodiscard]] bool load_library() noexcept;
void load_instance(VkInstance instance) noexcept;
void load_device(VkDevice device) noexcept;
void unload_library() noexcept;

} // namespace aurea::vk

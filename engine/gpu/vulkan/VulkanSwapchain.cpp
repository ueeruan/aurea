// =============================================================================
//  Superfície e swapchain.
//
//  Android: SurfaceView → Surface → ANativeWindow → VkSurfaceKHR → swapchain.
//  A UI só posiciona a SurfaceView; os pixels vão direto do compositor para a
//  janela, sem Bitmap e sem passar pelo Compose.
//
//  Ciclo de vida: o sistema destrói a superfície ao ir para segundo plano e
//  entrega OUTRA ao voltar. Só o swapchain e a VkSurface são refeitos; o
//  dispositivo, os pipelines e as texturas continuam — voltar é instantâneo.
// =============================================================================
#include "VulkanBackend.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>

namespace aurea::vk {

Status Backend::attach_surface(const SurfaceDesc& desc) noexcept {
    if (!initialized_) return Status{Errc::InvalidState, "backend nao inicializado"};
    if (!desc.nativeWindow) return Status{Errc::InvalidArgument, "janela nula"};
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    if (!hasSurfaceExt_) return Status{Errc::UnsupportedFeature, "sem VK_KHR_android_surface"};
    detach_surface();
    surfaceDesc_ = desc;

    VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    info.window = static_cast<ANativeWindow*>(desc.nativeWindow);
    if (const Status s = check(vkCreateAndroidSurfaceKHR(instance_, &info, nullptr, &surface_),
                               "vkCreateAndroidSurfaceKHR"); !s.ok()) {
        surface_ = VK_NULL_HANDLE;
        return s;
    }
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_, graphicsFamily_, surface_, &supported);
    if (!supported) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
        return Status{Errc::UnsupportedFeature, "fila grafica nao apresenta nesta superficie"};
    }
    return create_swapchain(desc.width, desc.height);
#else
    (void)desc;
    return Status{Errc::NotSupported, "apresentacao nao suportada neste host"};
#endif
}

void Backend::detach_surface() noexcept {
    if (!device_) return;
    if (swapchain_ || surface_) {
        // A janela vai ser destruída pelo sistema logo depois desta chamada:
        // nada pode estar usando o swapchain.
        vkDeviceWaitIdle(device_);
        for (u32 i = 0; i < framesInFlight_; ++i) run_deferred(frames_[i]);
    }
    destroy_swapchain();
    if (surface_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
}

Status Backend::resize_surface(u32 width, u32 height) noexcept {
    surfaceDesc_.width = width;
    surfaceDesc_.height = height;
    if (!surface_) return OkStatus;
    swapchainDirty_ = true;
    return OkStatus;
}

Status Backend::recreate_swapchain() noexcept {
    vkDeviceWaitIdle(device_);
    return create_swapchain(surfaceDesc_.width, surfaceDesc_.height);
}

Status Backend::create_swapchain(u32 width, u32 height) noexcept {
    if (!surface_) return Status{Errc::SurfaceLost, "sem superficie"};

    VkSurfaceCapabilitiesKHR caps{};
    if (const Status s = check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps),
                               "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"); !s.ok()) {
        return s;
    }

    u32 fc = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fc, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fc);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fc, formats.data());
    // UNORM, não SRGB: a codificação sRGB é do passe de saída (a mesma função
    // do export). Metade em hardware e metade em shader faria o preview e o
    // arquivo divergirem.
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_R8G8B8A8_UNORM,
                                                                      VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                                                : formats[0];
    for (const VkSurfaceFormatKHR& f : formats) {
        if ((f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM)
            && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }

    // A extensão atual já vem na orientação NATIVA do painel. Com a
    // pré-rotação, o conteúdo é girado no shader e o compositor do sistema não
    // precisa gastar um passe girando a imagem.
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) return Status{Errc::SurfaceLost, "superficie com tamanho zero"};

    VkSurfaceTransformFlagBitsKHR transform = caps.currentTransform;
    const bool rotated = transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR
                      || transform == VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR
                      || transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
    if (!rotated || !(caps.supportedTransforms & transform)) transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    if (!(caps.supportedTransforms & transform)) transform = caps.currentTransform;

    u32 imageCount = std::max(3u, caps.minImageCount);
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & alpha)) alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = transform;
    info.compositeAlpha = alpha;
    // FIFO sempre: é o único modo garantido, e é ele que dá o ritmo do vsync
    // à thread de render (a aquisição espera) — sem queimar bateria desenhando
    // frames que o painel não mostra.
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_;

    VkSwapchainKHR created = VK_NULL_HANDLE;
    if (const Status s = check(vkCreateSwapchainKHR(device_, &info, nullptr, &created), "vkCreateSwapchainKHR");
        !s.ok()) {
        return s;
    }
    // O antigo (e suas views) sai só depois que o novo existe: foi passado como
    // `oldSwapchain`, e o driver pode reaproveitar os buffers dele.
    destroy_swapchain();
    swapchain_ = created;
    swapFormat_ = chosen.format;
    swapExtent_ = extent;
    swapTransform_ = transform;

    u32 count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    std::vector<VkImage> images(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, images.data());

    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkImage img : images) {
        Texture t;
        t.image = img;
        t.ownsImage = false;
        t.format = chosen.format;
        t.desc.width = extent.width;
        t.desc.height = extent.height;
        t.desc.format = from_vk(chosen.format);
        t.desc.renderTarget = true;
        t.desc.sampled = false;
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = chosen.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &vi, nullptr, &t.view) != VK_SUCCESS) {
            return Status{Errc::SurfaceLost, "view do swapchain"};
        }
        swapTextures_.push_back(textures_.add(std::move(t)));
        // O semáforo de "render terminou" é POR IMAGEM: um só para todas
        // falha sob carga (a apresentação da imagem N ainda o espera quando o
        // frame N+1 já quer sinalizá-lo), e o sintoma é um frame rasgado.
        VkSemaphore sem = VK_NULL_HANDLE;
        (void)vkCreateSemaphore(device_, &si, nullptr, &sem);
        renderDone_.push_back(sem);
    }
    swapchainDirty_ = false;
    AUREA_LOG_INFO("swapchain: %ux%u, %u imagens, rotacao %d", extent.width, extent.height, count,
                   static_cast<int>(transform));
    return OkStatus;
}

void Backend::destroy_swapchain() noexcept {
    for (u64 id : swapTextures_) {
        Texture t;
        if (textures_.remove(id, t)) {
            for (VkFramebuffer fb : t.framebuffers) if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
            if (t.view) vkDestroyImageView(device_, t.view, nullptr);
        }
    }
    swapTextures_.clear();
    for (VkSemaphore s : renderDone_) if (s) vkDestroySemaphore(device_, s, nullptr);
    renderDone_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    imageAcquired_ = false;
}

} // namespace aurea::vk

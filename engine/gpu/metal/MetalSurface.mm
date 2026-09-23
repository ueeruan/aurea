// =============================================================================
//  Superfície: CAMetalLayer.
//
//  iOS: a camada (Metal) do UIView → `nextDrawable` → textura do sistema. Não há
//  swapchain para criar, nem extensão de plataforma, nem recriação por rotação:
//  a CAMetalLayer tem três drawables por conta própria e o sistema gira a
//  camada. É por isso que `attach_surface` aqui é curto.
//
//  Ciclo de vida: o app indo para segundo plano não destrói a camada, mas a view
//  pode ser refeita (rotação, multitarefa) e a camada nova chega em outro
//  `attach_surface`. Só a camada é trocada: pipelines, texturas e caches
//  continuam — voltar é instantâneo, igual ao Android.
//
//  THREAD: `nextDrawable` é seguro de qualquer thread (é o coração do render
//  loop), mas mexer nas PROPRIEDADES da camada enquanto outro frame é gravado
//  não é: `attach_surface` e `resize_surface` rodam na thread de render.
// =============================================================================
#include "MetalInternal.hpp"

#include "aurea/core/Log.hpp"

namespace aurea::mtl {

Status Backend::attach_surface(const SurfaceDesc& desc) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (!d.initialized) return Status{Errc::InvalidState, "backend nao inicializado"};
        if (!desc.nativeWindow) return Status{Errc::InvalidArgument, "janela nula"};

        id<CAMetalLayer> layer = (__bridge id<CAMetalLayer>)desc.nativeWindow;
        if (![layer isKindOfClass:[CAMetalLayer class]]) {
            return Status{Errc::InvalidArgument, "nativeWindow nao e CAMetalLayer"};
        }
        detach_surface();
        d.surfaceDesc = desc;
        layer.device = d.device;
        // UNORM, não sRGB: a codificação sRGB é do passe de saída (a mesma função
        // do export). Metade em hardware e metade em shader faria o preview e o
        // arquivo divergirem — a mesma razão do swapchain escolhido no Vulkan.
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        // A textura do drawable é lida de volta (leitura de volta dos testes,
        // preview em textura) — sem isto o Metal só a permite como alvo.
        layer.framebufferOnly = NO;
        // Três drawables: um a mais que os frames em voo, para a apresentação do
        // frame N não segurar a aquisição do N+1.
        layer.maximumDrawableCount = 3;
        layer.displaySyncEnabled = desc.vsync ? YES : NO;
        layer.allowsNextDrawableTimeout = YES;
        if (desc.width > 0 && desc.height > 0) {
            layer.drawableSize = CGSizeMake(static_cast<CGFloat>(desc.width), static_cast<CGFloat>(desc.height));
        }
        d.layer = layer;
        AUREA_LOG_INFO("metal: superficie %ux%u, vsync %d", desc.width, desc.height, desc.vsync ? 1 : 0);
        return OkStatus;
    }
}

void Backend::detach_surface() noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (!d.layer && !d.drawable) return;
        if (d.device && d.initialized) {
            // A camada pode ser destruída pelo sistema logo depois desta chamada:
            // nada pode estar usando o drawable.
            for (u32 i = 0; i < 3; ++i) {
                if (d.frames[i].submitted) d.wait_frame_gpu(d.frames[i]);
            }
        }
        d.drawable = nil;
        d.drawableAcquired = false;
        d.release_drawable_textures();
        d.layer = nil;
    }
}

Status Backend::resize_surface(u32 width, u32 height) noexcept {
    Impl& d = *impl_;
    d.surfaceDesc.width = width;
    d.surfaceDesc.height = height;
    if (!d.layer) return OkStatus;
    if (width > 0 && height > 0) {
        // O tamanho do `drawableSize` é em PIXELS (o tamanho lógico da view vezes
        // o contentsScale já foi aplicado por quem chamou).
        d.layer.drawableSize = CGSizeMake(static_cast<CGFloat>(width), static_cast<CGFloat>(height));
    }
    return OkStatus;
}

bool Backend::has_surface() const noexcept { return impl_->layer != nil; }

// =============================================================================
// Textura do drawable como recurso do pool
//
// O motor enxerga o backbuffer como um `TextureHandle` qualquer: `bind_texture`
// no slot do passe final, `texture_desc` para o FrameGraph. O objeto MTLTexture
// muda a cada aquisição, então o handle é ESTÁVEL por slot (3, um por frame em
// voo) e aponta para a textura da vez — o mesmo desenho das imagens do swapchain
// no Vulkan.
// =============================================================================
TextureHandle Impl::register_drawable(id<MTLTexture> texture, u32 width, u32 height) noexcept {
    const u32 slotIndex = drawableCursor % 3u;
    drawableCursor = (drawableCursor + 1u) % 3u;
    u64 id = drawableHandles[slotIndex];
    Texture* t = id ? textures.get(id) : nullptr;
    if (!t) {
        Texture fresh;
        fresh.ownsTexture = false;
        id = textures.add(std::move(fresh));
        drawableHandles[slotIndex] = id;
        t = textures.get(id);
        if (!t) return TextureHandle{};
    }
    t->texture = texture;
    t->ownsTexture = false;
    t->external = false;
    t->format = layer ? layer.pixelFormat : MTLPixelFormatBGRA8Unorm;
    // Conteúdo anterior do drawable não interessa: o passe de saída limpa.
    t->state = ResourceState::Undefined;
    t->desc = TextureDesc{};
    t->desc.width = width;
    t->desc.height = height;
    t->desc.format = from_mtl(t->format);
    t->desc.renderTarget = true;
    t->desc.sampled = false;
    t->desc.debugName = "drawable";
    t->lastUsedFrame = frameNumber;
    return TextureHandle{id};
}

void Impl::release_drawable_textures() noexcept {
    for (u64& id : drawableHandles) {
        if (!id) continue;
        Texture dead;
        if (textures.remove(id, dead)) {
            // Não é nossa: solta a referência e a memória contábil, sem destruir
            // nada do sistema.
            dead.texture = nil;
        }
        id = 0;
    }
    drawableCursor = 0;
}

} // namespace aurea::mtl

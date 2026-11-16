// =============================================================================
//  Apresentação no host (Aurea PC).
//
//  No Android o quadro do preview vai do passe de saída para o swapchain do
//  SurfaceView e a UI desenha os gizmos por cima. No PC a interface (Compose
//  Desktop, Skia sobre Direct3D 12) precisa desenhar POR CIMA do palco, então
//  o motor não apresenta numa janela:
//
//    1. a thread de render desenha o quadro EXATAMENTE como desenharia no
//       swapchain (mesmo grafo, mesmo passe de saída, sRGB no shader), só que
//       num anel de imagens do motor — uma por contexto de frame;
//    2. no fim do mesmo envio a GPU copia a imagem para um buffer da CPU
//       mapeado o tempo todo (BGRA8, linhas sem folga);
//    3. um fence por buffer diz quando a cópia terminou; a UI pega o quadro
//       terminado mais novo. O mutex só protege o estado dos slots (nunca há
//       espera de GPU nem de cópia da UI dentro dele), e há slots de sobra
//       para a thread de render nunca esperar a UI.
//
//  O ritmo segue o vblank REAL do monitor (o relógio do DWM, o mesmo em que a
//  UI desenha): no máximo um quadro por vblank, começando a um terço do
//  intervalo. Assim o quadro fica pronto longe do instante em que a UI o pega
//  (logo depois do vblank) e a cadência não "bate" contra um relógio próprio
//  — antes um timer de 60 Hz fixo contra o vsync do DWM duplicava e perdia
//  quadros de tempos em tempos. Sem DWM: timer na taxa informada pela UI.
//  Parado, o motor não desenha (a render_frame pula quando nada mudou) e aqui
//  nada roda: custo zero.
// =============================================================================
#if defined(_WIN32)
// Antes do Vulkan: o dwmapi.h puxa o commctrl.h, que não aceita o `near` desfeito lá.
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif
#include "VulkanBackend.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/core/Time.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>


namespace aurea::vk {
namespace {
// Só o endereço importa: é a "janela" que liga a apresentação no host.
char gHostPresentTag = 0;

void sleep_until_ns(void* timer, u64 deadline) noexcept {
    const u64 now = monotonic_ns();
    if (deadline <= now) return;
#if defined(_WIN32)
    if (timer) {
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>((deadline - now + 99) / 100);   // unidades de 100 ns, relativo
        if (SetWaitableTimerEx(static_cast<HANDLE>(timer), &due, 0, nullptr, nullptr, nullptr, 0)) {
            WaitForSingleObject(static_cast<HANDLE>(timer), INFINITE);
            return;
        }
    }
#else
    (void)timer;
#endif
    std::this_thread::sleep_for(std::chrono::nanoseconds(deadline - now));
}

// Último vblank do DWM no relógio do motor (monotonic_ns) e o período real.
bool dwm_vblank(i64& vblankNs, i64& periodNs) noexcept {
#if defined(_WIN32)
    DWM_TIMING_INFO ti{};
    ti.cbSize = sizeof(ti);
    if (FAILED(DwmGetCompositionTimingInfo(nullptr, &ti)) || !ti.qpcRefreshPeriod || !ti.qpcVBlank) return false;
    LARGE_INTEGER now{}, freq{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    const i64 mono = static_cast<i64>(monotonic_ns());
    const f64 toNs = 1e9 / static_cast<f64>(freq.QuadPart);
    periodNs = static_cast<i64>(static_cast<f64>(ti.qpcRefreshPeriod) * toNs);
    vblankNs = mono - static_cast<i64>((static_cast<f64>(now.QuadPart) - static_cast<f64>(ti.qpcVBlank)) * toNs);
    return periodNs >= 2'000'000 && periodNs <= 50'000'000;   // 20 a 500 Hz
#else
    (void)vblankNs; (void)periodNs;
    return false;
#endif
}
} // namespace

void* Backend::host_present_window() noexcept { return &gHostPresentTag; }

void Backend::set_host_refresh_rate(f32 hz) noexcept {
    if (!(hz >= 24.0f && hz <= 500.0f)) hz = 60.0f;
    std::lock_guard<std::mutex> lock(host_.mutex);
    host_.periodNs = static_cast<u64>(1e9 / static_cast<f64>(hz));
}

Status Backend::attach_host_present(u32 width, u32 height) noexcept {
    if (!width || !height) return Status{Errc::InvalidArgument, "superficie do host com tamanho zero"};
    {
        std::lock_guard<std::mutex> lock(host_.mutex);
        host_.width = width;
        host_.height = height;
        host_.dirty = true;
        host_.active = true;
        host_.nextTickNs = 0;
        host_.lastSlotNs = host_.lastFrameNs = 0;
    }
#if defined(_WIN32)
    if (!host_.timer)
        host_.timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!host_.timer) host_.timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
#endif
    AUREA_LOG_INFO("apresentacao no host: %ux%u, %u imagens, %u buffers", width, height, framesInFlight_, kHostSlots);
    return OkStatus;
}

void Backend::destroy_host_slot(HostSlot& s) noexcept {
    if (!device_) return;
    if (s.mapped && s.memory) deviceFunctions_.vkUnmapMemory(device_, s.memory);
    if (s.buffer) deviceFunctions_.vkDestroyBuffer(device_, s.buffer, nullptr);
    if (s.memory) deviceFunctions_.vkFreeMemory(device_, s.memory, nullptr);
    s.mapped = nullptr;
    s.buffer = VK_NULL_HANDLE;
    s.memory = VK_NULL_HANDLE;
    s.bytes = 0;
}

void Backend::detach_host_present() noexcept {
    bool any = false;
    {
        std::lock_guard<std::mutex> lock(host_.mutex);
        any = host_.active;
        for (const u64 id : host_.images) any = any || id != 0;
        for (const HostSlot& s : host_.slots) any = any || s.buffer || s.fence;
        host_.active = false;
    }
    if (!any) return;
    // Os buffers e imagens podem estar na GPU (cópias em voo): espera tudo.
    if (device_) deviceFunctions_.vkDeviceWaitIdle(device_);
    for (u64& id : host_.images) {
        Texture t;
        if (id && textures_.remove(id, t)) destroy_texture_now(t);
        id = 0;
    }
    host_.imageWidth = host_.imageHeight = 0;
    host_.acquired = false;
    std::unique_lock<std::mutex> lock(host_.mutex);
    // A UI pode estar copiando um quadro: a espera é de um memcpy, não da GPU.
    host_.readersDone.wait(lock, [this] {
        for (const HostSlot& s : host_.slots) if (s.readers) return false;
        return true;
    });
    for (HostSlot& s : host_.slots) {
        destroy_host_slot(s);
        if (s.fence && device_) deviceFunctions_.vkDestroyFence(device_, s.fence, nullptr);
        s.fence = VK_NULL_HANDLE;
        s.state = HostSlot::State::Free;
        s.serial = 0;
        s.width = s.height = 0;
    }
    host_.pendingSlot = -1;
    host_.nextTickNs = 0;
    host_.lastSlotNs = host_.lastFrameNs = 0;
#if defined(_WIN32)
    if (host_.timer) CloseHandle(static_cast<HANDLE>(host_.timer));
#endif
    host_.timer = nullptr;
}

Status Backend::prepare_host_images() noexcept {
    u32 width = 0, height = 0;
    bool dirty = false;
    {
        std::lock_guard<std::mutex> lock(host_.mutex);
        width = host_.width;
        height = host_.height;
        dirty = host_.dirty;
        host_.dirty = false;
    }
    bool missing = false;
    for (u32 i = 0; i < framesInFlight_; ++i) missing = missing || !host_.images[i];
    if (!dirty && !missing) return OkStatus;
    // As imagens antigas saem pela fila adiada do quadro aberto: ele é enviado
    // depois de todos os que ainda as usam (fila única).
    for (u64& id : host_.images) {
        if (id) destroy_texture(TextureHandle{id});
        id = 0;
    }
    TextureDesc d;
    d.width = width;
    d.height = height;
    d.format = SurfaceFormat::BGRA8;
    d.sampled = false;
    d.renderTarget = true;
    d.transferSrc = true;
    d.debugName = "apresentacao-host";
    for (u32 i = 0; i < framesInFlight_; ++i) {
        auto t = create_texture(d);
        if (!t.ok()) {
            std::lock_guard<std::mutex> lock(host_.mutex);
            host_.dirty = true;   // tenta de novo no próximo quadro
            return t.status();
        }
        host_.images[i] = t->id;
    }
    host_.imageWidth = width;
    host_.imageHeight = height;
    return OkStatus;
}

void Backend::host_pace() noexcept {
    u64 period = 0;
    {
        std::lock_guard<std::mutex> lock(host_.mutex);
        period = host_.periodNs;
    }
    const u64 now = monotonic_ns();
    // AUREA_HOST_PACE=timer volta ao timer próprio (comparação/diagnóstico).
    static const bool timerOnly = [] {
        const char* v = std::getenv("AUREA_HOST_PACE");
        return v && std::strcmp(v, "timer") == 0;
    }();
    i64 vblank = 0, p = 0;
    if (!timerOnly && dwm_vblank(vblank, p)) {
        // Janelas de início: de V+P/3 a V+2P/3 de cada intervalo. A UI pega o
        // quadro logo depois do vblank; pronto no meio do intervalo, ele nunca
        // disputa esse instante (quadro de até ~2/3 do período).
        const i64 t = static_cast<i64>(now), off = p / 3;
        const i64 rel = t - vblank - off;
        const i64 k = rel >= 0 ? rel / p : -((-rel + p - 1) / p);
        const i64 cur = vblank + off + k * p;   // início da janela atual (≤ agora)
        const bool idle = !host_.lastFrameNs || now - host_.lastFrameNs > 100'000'000ull;
        i64 slot = cur;
        // Primeiro quadro depois de parado (edição, seek): sai na hora. Tocando,
        // um por vblank: dentro da janela sai já; depois dela, espera a próxima.
        if (!idle && (t - cur > off || cur < host_.lastSlotNs + p / 2)) {
            slot = cur + p;
            if (slot < host_.lastSlotNs + p / 2) slot += p;
        }
        if (slot > t) sleep_until_ns(host_.timer, static_cast<u64>(slot));
        host_.lastSlotNs = slot;
        host_.lastFrameNs = monotonic_ns();
        host_.nextTickNs = 0;
        return;
    }
    // Como a aquisição FIFO: quadro atrasado (ou o primeiro depois de parado)
    // sai na hora; os seguintes, um por período, na fase do primeiro.
    if (host_.nextTickNs == 0 || now >= host_.nextTickNs + period) {
        host_.nextTickNs = now + period;
        return;
    }
    if (now < host_.nextTickNs) sleep_until_ns(host_.timer, host_.nextTickNs);
    host_.nextTickNs += period;
}

void Backend::host_begin_frame(FrameContext& f, FrameBegin& out) noexcept {
    host_.acquired = false;
    host_pace();
    if (const Status s = prepare_host_images(); !s.ok()) {
        AUREA_LOG_WARN("apresentacao no host: alvo nao criado (%s)", s.message().data());
        return;   // quadro sem backbuffer: gravado e enviado vazio, como sem imagem do swapchain
    }
    const u32 index = static_cast<u32>(&f - frames_);
    Texture* t = index < 3 ? textures_.get(host_.images[index]) : nullptr;
    if (!t) return;
    // O conteúdo anterior não interessa: o passe de saída limpa.
    t->state = ResourceState::Undefined;
    t->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    host_.acquired = true;
    host_.imageIndex = index;
    out.backbuffer = TextureHandle{host_.images[index]};
    out.backbufferWidth = host_.imageWidth;
    out.backbufferHeight = host_.imageHeight;
    out.backbufferFormat = SurfaceFormat::BGRA8;
    out.rotation = SurfaceRotation::None;
}

void Backend::host_refresh_locked() noexcept {
    for (u32 i = 0; i < kHostSlots; ++i) {
        HostSlot& s = host_.slots[i];
        // O slot pendente ainda não teve o fence enviado (o envio é da render).
        if (s.state != HostSlot::State::InFlight || static_cast<i32>(i) == host_.pendingSlot || !s.fence) continue;
        if (deviceFunctions_.vkWaitForFences(device_, 1, &s.fence, VK_TRUE, 0) == VK_SUCCESS)
            s.state = HostSlot::State::Ready;
    }
}

void Backend::host_record_copy(FrameContext& f) noexcept {
    Texture* t = textures_.get(host_.images[host_.imageIndex]);
    // Nada desenhou no alvo (grafo vazio, erro): não há quadro a publicar.
    if (!t || t->state == ResourceState::Undefined) return;
    const u32 width = t->desc.width, height = t->desc.height;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;

    i32 chosen = -1;
    {
        std::lock_guard<std::mutex> lock(host_.mutex);
        if (host_.pendingSlot >= 0) {
            // O envio anterior falhou depois da gravação: o slot volta livre.
            host_.slots[host_.pendingSlot].state = HostSlot::State::Free;
            host_.pendingSlot = -1;
        }
        host_refresh_locked();
        u64 newest = 0;
        for (const HostSlot& s : host_.slots)
            if (s.state == HostSlot::State::Ready) newest = std::max(newest, s.serial);
        // Livre e do tamanho certo > livre > o pronto mais velho (nunca o mais
        // novo, que a UI ainda vai pegar, nem um que ela esteja lendo).
        for (u32 pass = 0; pass < 3 && chosen < 0; ++pass) {
            u64 oldest = ~0ull;
            for (u32 i = 0; i < kHostSlots; ++i) {
                const HostSlot& s = host_.slots[i];
                if (s.readers) continue;
                if (pass == 0 && s.state == HostSlot::State::Free && s.bytes >= bytes) { chosen = static_cast<i32>(i); break; }
                if (pass == 1 && s.state == HostSlot::State::Free) { chosen = static_cast<i32>(i); break; }
                if (pass == 2 && s.state == HostSlot::State::Ready && s.serial != newest && s.serial < oldest) {
                    oldest = s.serial;
                    chosen = static_cast<i32>(i);
                }
            }
        }
        if (chosen < 0) return;   // todos ocupados (não deveria acontecer): quadro não publicado
        HostSlot& s = host_.slots[chosen];
        s.state = HostSlot::State::InFlight;
        s.serial = ++host_.nextSerial;
        s.width = width;
        s.height = height;
        if (s.fence) (void)deviceFunctions_.vkResetFences(device_, 1, &s.fence);
        host_.pendingSlot = chosen;
    }
    HostSlot& s = host_.slots[chosen];
    const auto abandon = [&](const char* what) noexcept {
        AUREA_LOG_WARN("apresentacao no host: %s", what);
        std::lock_guard<std::mutex> lock(host_.mutex);
        s.state = HostSlot::State::Free;
        host_.pendingSlot = -1;
    };
    if (!s.fence) {
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (deviceFunctions_.vkCreateFence(device_, &fi, nullptr, &s.fence) != VK_SUCCESS) {
            s.fence = VK_NULL_HANDLE;
            return abandon("fence nao criado");
        }
    }
    if (s.bytes < bytes) {
        // Slot livre ou já lido e terminado: nem GPU nem UI tocam nele agora.
        destroy_host_slot(s);
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (deviceFunctions_.vkCreateBuffer(device_, &bi, nullptr, &s.buffer) != VK_SUCCESS) {
            s.buffer = VK_NULL_HANDLE;
            return abandon("buffer nao criado");
        }
        VkMemoryRequirements req{};
        deviceFunctions_.vkGetBufferMemoryRequirements(device_, s.buffer, &req);
        const VkPhysicalDeviceMemoryProperties& props = allocator_.properties();
        // Leitura pela CPU: memória com cache primeiro (ler memória
        // write-combined é dezenas de vezes mais lento).
        const VkMemoryPropertyFlags wanted[] = {
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        };
        u32 type = kInvalidIndex;
        for (const VkMemoryPropertyFlags flags : wanted) {
            for (u32 i = 0; i < props.memoryTypeCount && type == kInvalidIndex; ++i)
                if ((req.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) type = i;
            if (type != kInvalidIndex) break;
        }
        if (type == kInvalidIndex) return abandon("sem memoria visivel pela CPU");
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = type;
        if (deviceFunctions_.vkAllocateMemory(device_, &ai, nullptr, &s.memory) != VK_SUCCESS) {
            s.memory = VK_NULL_HANDLE;
            destroy_host_slot(s);
            return abandon("memoria do buffer recusada");
        }
        if (deviceFunctions_.vkBindBufferMemory(device_, s.buffer, s.memory, 0) != VK_SUCCESS
            || deviceFunctions_.vkMapMemory(device_, s.memory, 0, VK_WHOLE_SIZE, 0, &s.mapped) != VK_SUCCESS) {
            s.mapped = nullptr;
            destroy_host_slot(s);
            return abandon("buffer nao mapeado");
        }
        s.bytes = bytes;
        s.coherent = (props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    }

    // Escrita do passe de saída → leitura da cópia (vindo de Present ou de
    // anexo de cor, conforme o grafo terminou).
    VkImageMemoryBarrier ib{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ib.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    ib.oldLayout = t->layout;
    ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = t->image;
    ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    deviceFunctions_.vkCmdPipelineBarrier(f.cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    t->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    t->state = ResourceState::TransferSrc;

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    deviceFunctions_.vkCmdCopyImageToBuffer(f.cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.buffer, 1, &region);
    VkBufferMemoryBarrier hb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hb.srcQueueFamilyIndex = hb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hb.buffer = s.buffer;
    hb.offset = 0;
    hb.size = VK_WHOLE_SIZE;
    deviceFunctions_.vkCmdPipelineBarrier(f.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                                          1, &hb, 0, nullptr);
}

void Backend::host_after_submit() noexcept {
    i32 slot = -1;
    {
        std::lock_guard<std::mutex> lock(host_.mutex);
        slot = host_.pendingSlot;
    }
    if (slot < 0) return;
    // Envio vazio com fence: o sinal cobre todo trabalho anterior da fila,
    // inclusive a cópia que acabou de ir. A UI só olha este fence depois que
    // `pendingSlot` é solto (o fence do envio é de acesso exclusivo).
    const VkResult r = deviceFunctions_.vkQueueSubmit(queue_, 0, nullptr, host_.slots[slot].fence);
    std::lock_guard<std::mutex> lock(host_.mutex);
    host_.pendingSlot = -1;
    if (r != VK_SUCCESS) host_.slots[slot].state = HostSlot::State::Free;
}

u64 Backend::copy_host_frame(void* dst, usize capacity, HostFrameInfo& info) noexcept {
    info = {};
    HostSlot* slot = nullptr;
    u64 serial = 0;
    {
        std::lock_guard<std::mutex> lock(host_.mutex);
        if (!host_.active || !device_) return 0;
        host_refresh_locked();
        for (HostSlot& s : host_.slots)
            if (s.state == HostSlot::State::Ready && (!slot || s.serial > slot->serial)) slot = &s;
        if (!slot || slot->serial <= host_.delivered) return 0;
        info.width = slot->width;
        info.height = slot->height;
        info.rowBytes = slot->width * 4;
        if (!dst || capacity < static_cast<usize>(info.rowBytes) * info.height) return 0;
        ++slot->readers;
        serial = slot->serial;
    }
    if (!slot->coherent) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = slot->memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        (void)deviceFunctions_.vkInvalidateMappedMemoryRanges(device_, 1, &range);
    }
    std::memcpy(dst, slot->mapped, static_cast<usize>(info.rowBytes) * info.height);
    {
        std::lock_guard<std::mutex> lock(host_.mutex);
        --slot->readers;
        host_.delivered = std::max(host_.delivered, serial);
    }
    host_.readersDone.notify_all();
    return serial;
}

} // namespace aurea::vk

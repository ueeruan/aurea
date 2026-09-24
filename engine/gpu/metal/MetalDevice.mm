// =============================================================================
//  Dispositivo, capacidades, contextos de frame, medição, cache de pipeline e
//  ciclo de vida.
//
//  O device é um só (iOS tem uma GPU), a fila é uma só, e cada frame em voo tem
//  o seu command buffer com um fence de conclusão — o fence por frame do
//  Vulkan, com a mesma semântica: espera o frame N sem parar a fila inteira, e
//  aceita tempo limite (é o que o export usa para ler o quadro N enquanto a GPU
//  já trabalha no N+1).
// =============================================================================
#include "MetalInternal.hpp"

#include "aurea/core/Log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace aurea::mtl {

// =============================================================================
// Ciclo de vida
// =============================================================================
Status Backend::initialize(const BackendConfig& config) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (d.initialized) return OkStatus;
        d.config = config;
        d.framesInFlight = std::clamp<u32>(config.framesInFlight, 1, 3);
        d.deviceLost = false;

        d.device = d.requestedDevice ? (__bridge id<MTLDevice>)d.requestedDevice
                                     : MTLCreateSystemDefaultDevice();
        if (!d.device) return Status{Errc::NotSupported, "nenhum dispositivo Metal"};
        AUREA_LOG_INFO("metal: iniciando GPU=%s, descricao=%s, simulator=%d",
                       d.device.name.UTF8String ?: "", d.device.description.UTF8String ?: "",
                       TARGET_OS_SIMULATOR ? 1 : 0);

        d.queue = [d.device newCommandQueue];
        if (!d.queue) return Status{Errc::NotSupported, "sem fila Metal"};
        d.queue.label = @"aurea";

        // Zero-copy de vídeo: o cache embrulha o IOSurface do CVPixelBuffer numa
        // MTLTexture. Sem ele, o vídeo externo simplesmente não existe — e os
        // planos de CPU continuam funcionando (é o mesmo par de caminhos do
        // Android com e sem AHardwareBuffer).
        if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nil, d.device, nil, &d.textureCache)
            != kCVReturnSuccess) {
            d.textureCache = nullptr;
            AUREA_LOG_WARN("metal: sem CVMetalTextureCache — video zero-copy desligado");
        }

        if (config.enableValidation) {
            // Metal não tem camada de validação pedida por API: quem liga é o
            // esquema do Xcode (Metal API Validation) ou MTL_DEBUG_LAYER=1 no
            // ambiente. Log honesto, em vez de `caps_.validationEnabled = true`.
            AUREA_LOG_INFO("metal: validacao vem do esquema do Xcode (MTL_DEBUG_LAYER), nao da API");
        }

        d.fill_capabilities();
        d.load_pipeline_cache();
        if (const Status s = d.create_frames(); !s.ok()) return s;
        if (const Status s = d.create_dummies(); !s.ok()) return s;

        d.initialized = true;
        AUREA_LOG_INFO("metal: %s", d.caps.summary().c_str());
        return OkStatus;
    }
}

void Backend::shutdown() noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (!d.device && !d.initialized) return;

        // 1. Nada pode estar em voo quando os recursos começam a morrer — e a
        //    fila de destruição adiada tem de rodar (senão o que estava adiado
        //    fica pendurado para sempre).
        for (u32 i = 0; i < 3; ++i) {
            if (d.frames[i].submitted) d.wait_frame_gpu(d.frames[i]);
        }
        d.wait_immediate_gpu();
        d.current = nullptr;
        d.lastSubmitted = nullptr;
        for (u32 i = 0; i < 3; ++i) d.run_deferred(d.frames[i]);

        // 2. A superfície antes dos recursos: as texturas do drawable morrem com
        //    a layer.
        d.release_drawable_textures();
        d.drawable = nil;
        d.layer = nil;

        if (d.device) {
            save_pipeline_cache();

            d.textures.for_each([&d](u64, Texture& t) { d.destroy_texture_now(t); });
            d.textures.clear();
            d.buffers.for_each([&d](u64, Buffer& b) { d.destroy_buffer_now(b); });
            d.buffers.clear();
            d.samplers.for_each([&d](u64, SamplerObject& s) { s.sampler = nil; });
            d.samplers.clear();
            d.pipelines.for_each([&d](u64, PipelineObject& p) {
                p.render = nil;
                p.compute = nil;
                p.depth = nil;
            });
            d.pipelines.clear();
            d.shaders.for_each([&d](u64, ShaderObject& s) {
                s.function = nil;
                s.library = nil;
            });
            d.shaders.clear();
            d.depthStates.clear();
            if (d.defaultSampler) d.defaultSampler = nil;
            d.dummyTexture = d.dummyStorage = d.dummyBuffer = 0;
            d.importedByBuffer.clear();
            d.destroy_frames();
            d.archive = nil;
            d.timestampSet = nil;

            if (d.textureCache) {
                CVMetalTextureCacheFlush(d.textureCache, 0);
                CFRelease(d.textureCache);
                d.textureCache = nullptr;
            }
            d.queue = nil;
            d.device = nil;
        }
        d.initialized = false;
        d.textureBytes = d.bufferBytes = 0;
        d.allocationCount = 0;
        d.timings.clear();
        d.timingsValid = false;
    }
}

const char* Backend::name() const noexcept { return "Metal"; }
const GPUCapabilities& Backend::capabilities() const noexcept { return impl_->caps; }

bool Backend::is_device_lost() const noexcept { return impl_->deviceLost; }
u32 Backend::frames_in_flight() const noexcept { return impl_->framesInFlight; }

// =============================================================================
// Capacidades
//
// Toda pergunta "o aparelho suporta X?" é respondida AQUI, uma vez. O que o
// Metal não expõe por API (limite de textura 2D, número de imagens de
// armazenamento por estágio) entra com o valor garantido pela especificação da
// família, e está dito no comentário — em vez de ser descoberto com um teste em
// runtime que pode mentir.
// =============================================================================
void Impl::fill_capabilities() noexcept {
    id<MTLDevice> dev = device;
    const bool apple = [dev supportsFamily:MTLGPUFamilyApple1];
    const bool mac2 = [dev supportsFamily:MTLGPUFamilyMac2];
    const NSOperatingSystemVersion os = NSProcessInfo.processInfo.operatingSystemVersion;

    caps.apiName = "Metal";
    // A versão da API no iOS/macOS acompanha a do sistema: o Metal não tem um
    // número de versão próprio consultável.
    caps.apiMajor = static_cast<u32>(std::max<NSInteger>(0, os.majorVersion));
    caps.apiMinor = static_cast<u32>(std::max<NSInteger>(0, os.minorVersion));
    caps.apiPatch = static_cast<u32>(std::max<NSInteger>(0, os.patchVersion));
    caps.deviceName = dev.name ? dev.name.UTF8String : "GPU";
    char driver[128];
    std::snprintf(driver, sizeof(driver), "%s · iOS/macOS %ld.%ld",
                  apple ? "Apple GPU" : (mac2 ? "Mac GPU" : "GPU"),
                  static_cast<long>(os.majorVersion), static_cast<long>(os.minorVersion));
    caps.driverInfo = driver;
    // O MTLDevice não expõe identificador de fornecedor nem de dispositivo.
    caps.vendorId = 0;
    caps.deviceId = 0;
    caps.deviceType = dev.hasUnifiedMemory ? GpuDeviceType::Integrated : GpuDeviceType::Discrete;

    caps.fp16Arithmetic = apple || mac2;
    caps.fp16Storage = apple || mac2;
    caps.int16Arithmetic = [dev supportsFamily:MTLGPUFamilyApple4] || mac2;

    const MTLSize tg = dev.maxThreadsPerThreadgroup;
    caps.maxComputeWorkGroupInvocations = static_cast<u32>(tg.width * tg.height * tg.depth);
    caps.maxComputeWorkGroupSize[0] = static_cast<u32>(tg.width);
    caps.maxComputeWorkGroupSize[1] = static_cast<u32>(tg.height);
    caps.maxComputeWorkGroupSize[2] = static_cast<u32>(tg.depth);
    caps.maxComputeSharedMemoryBytes = static_cast<u32>(dev.maxThreadgroupMemoryLength);
    // 16384 é o teto garantido de textura 2D em toda GPU Metal (o Metal não
    // publica o limite por API).
    caps.maxTexture2D = 16384;
    // `setBytes:` aceita até 4 KB; o layout universal do motor usa 128.
    caps.maxPushConstantBytes = binding::kPushConstantBytes;
    // Maior bloco de constantes amarrado por `setBuffer:` numa GPU Apple.
    caps.maxUniformBufferRange = 65536;
    // O Metal não tem conjuntos de descritores: o layout universal é o conjunto.
    caps.maxBoundDescriptorSets = 1;
    // Limite de argumentos de textura por estágio de função no MSL.
    caps.maxPerStageSampledImages = 31;
    caps.maxPerStageStorageImages = 8;   // o layout do motor usa 2
    caps.maxColorAttachments = 8;
    // Deslocamento num buffer de constantes: 256 bytes é a exigência do Metal
    // (e é o que o anel de uniforms respeita).
    caps.minUniformBufferOffsetAlignment = slot::kRingAlignment;
    caps.colorSampleCountMask = 1u | ([dev supportsTextureSampleCount:4] ? 4u : 0u);

    caps.rgba16fRenderable = true;
    caps.rgba16fFilterable = true;
    caps.rgba16fStorage = apple || mac2;
    caps.r16UnormSampled = true;
    // O Metal não tem consulta de uso de formato: o `MTLSamplerDescriptor`
    // aceita até 16 e o driver satura no que a GPU faz.
    caps.maxSamplerAnisotropy = 16.0f;
    caps.depth32fAttachment = true;
    caps.depth24Attachment = false;   // o Metal não tem profundidade de 24 bits
    caps.depth32fSampled = true;
    caps.textureCompressionASTC = true;   // toda GPU Metal amostra ASTC
    caps.textureCompressionETC2 = false;  // ETC2 não existe no Metal
    caps.textureCompressionBC = mac2;     // BC só em Mac (e não em iOS)
    caps.maxVertexInputAttributes = 31;

    // --- Timestamps -----------------------------------------------------------
    // Amostrar contadores exige um ponto de amostragem: este backend amostra num
    // encoder de blit (é assim que se mede ENTRE passes, sem mexer no estado de
    // nenhum encoder de render). Stage/draw NÃO são alternativas: aceitar um
    // desses pontos e amostrar pelo blit causa assert no driver Metal.
    counterSampling = false;
    // Contadores são do iOS 14 / macOS 10.15 em diante. Abaixo disso o motor
    // simplesmente não mede — e `caps.timestampQueries` diz isso.
    if (@available(iOS 14.0, macOS 10.15, *)) {
        counterSampling = [dev supportsCounterSampling:MTLCounterSamplingPointAtBlitBoundary];
        counterPoint = MTLCounterSamplingPointAtBlitBoundary;
        for (id<MTLCounterSet> set in dev.counterSets) {
            if ([set.name isEqualToString:MTLCommonCounterSetTimestamp]) {
                timestampSet = set;
                break;
            }
        }
    }
    caps.timestampQueries = counterSampling && timestampSet != nil;
    // O timestamp do Metal já vem em nanossegundos (o `timestampPeriodNs` do
    // Vulkan existe porque lá a unidade é do aparelho).
    caps.timestampPeriodNs = 1.0f;
    timersEnabled = caps.timestampQueries && config.enableGpuTimers;
    if (config.enableGpuTimers && !caps.timestampQueries) {
        AUREA_LOG_WARN("metal: sem amostragem de timestamps — o painel DEV fica sem tempos de GPU");
    }

    caps.samplerYcbcrConversion = false;  // YUV usa os shaders de planos; zero-copy BGRA é direto
    caps.externalMemoryHardwareBuffer = textureCache != nullptr;   // CVPixelBuffer/IOSurface como textura
    caps.queueFamilyForeign = false;      // não existe fila estrangeira no Metal
    caps.externalSemaphoreFd = false;
    caps.validationEnabled = false;

    caps.extensions.clear();
    struct Family { MTLGPUFamily family; const char* name; };
    // Nomes de família presentes (diagnóstico). As famílias mais novas (Apple8
    // em diante) não entram: o nome que a GPU reporta já identifica o aparelho, e
    // citar constantes que só existem em SDK recente quebraria o build antigo.
    const Family families[] = {
        {MTLGPUFamilyApple7, "apple7"}, {MTLGPUFamilyApple6, "apple6"},
        {MTLGPUFamilyApple5, "apple5"}, {MTLGPUFamilyApple4, "apple4"},
        {MTLGPUFamilyMac2, "mac2"}, {MTLGPUFamilyCommon3, "common3"},
    };
    for (const Family& f : families) {
        if ([dev supportsFamily:f.family]) caps.extensions.emplace_back(f.name);
    }
    if (timestampSet) caps.extensions.emplace_back("timestamps");

    caps.heaps.clear();
    caps.deviceLocalBytes = caps.hostVisibleBytes = 0;
    const u64 workingSet = static_cast<u64>(dev.recommendedMaxWorkingSetSize);
    if (workingSet > 0) {
        caps.heaps.push_back(GpuMemoryHeap{workingSet, true});
        caps.deviceLocalBytes = workingSet;
    }
    caps.unifiedMemory = dev.hasUnifiedMemory;
    if (caps.unifiedMemory) caps.hostVisibleBytes = workingSet;
}

// =============================================================================
// Contextos de frame
// =============================================================================
Status Impl::create_frames() noexcept {
    for (u32 i = 0; i < framesInFlight; ++i) {
        FrameContext& f = frames[i];
        if (!f.uniforms.initialize(device, 256 * 1024, "anel-uniforms")
            || !f.staging.initialize(device, 4 * 1024 * 1024, "anel-staging")) {
            return Status{Errc::OutOfDeviceMemory, "sem memoria para os aneis do frame"};
        }
        if (timersEnabled && timestampSet) {
            if (@available(iOS 14.0, macOS 10.15, *)) {
                @autoreleasepool {
                    // Um buffer de amostras POR FRAME: o anterior ainda pode estar
                    // sendo lido pela GPU quando este é gravado.
                    // Duas marcas por passe (início e fim) mais as duas do frame.
                    const u32 wanted[3] = {2 + kMaxTimers * 2, 2 + 32 * 2, 2 + 8 * 2};
                    for (u32 samples : wanted) {
                        MTLCounterSampleBufferDescriptor* desc = [[MTLCounterSampleBufferDescriptor alloc] init];
                        desc.counterSet = timestampSet;
                        desc.storageMode = MTLStorageModeShared;
                        desc.sampleCount = samples;
                        desc.label = @"timers";
                        NSError* err = nil;
                        f.counterBuffer = [device newCounterSampleBufferWithDescriptor:desc error:&err];
                        if (f.counterBuffer) {
                            f.counterSamples = samples;
                            maxTimerSlots = (samples - 2) / 2;
                            if (samples != wanted[0]) {
                                AUREA_LOG_WARN("metal: o device limitou as marcas de tempo a %u passes", maxTimerSlots);
                            }
                            break;
                        }
                        (void)check_ns(err, "newCounterSampleBufferWithDescriptor");
                    }
                    if (!f.counterBuffer) {
                        maxTimerSlots = 0;
                        AUREA_LOG_WARN("metal: sem buffer de timestamps; o painel DEV fica sem tempos");
                    }
                }
            }
        }
        f.timerLabels.reserve(kMaxTimers);
        f.deferred.reserve(64);
    }
    return OkStatus;
}

void Impl::destroy_frames() noexcept {
    for (FrameContext& f : frames) {
        f.uniforms.shutdown();
        f.staging.shutdown();
        f.counterBuffer = nil;
        f.counterSamples = 0;
        f.timerLabels.clear();
        f.timerStack.clear();
        f.deferred.clear();
        f.cmd = nil;
        f.completion = nil;
        f.submitted = false;
        f.frameNumber = 0;
    }
}

// =============================================================================
// Recursos de reserva: o que um slot não amarrado enxerga.
//
// Em Metal, ler um argumento de textura não amarrado é comportamento indefinido
// (na prática, uma textura nula e um acesso inválido). Todo slot declarado num
// shader precisa de recurso amarrado, e a textura preta 1×1 / o buffer vazio
// garantem isso sem cada passe ter de pensar nos slots que não usa.
// =============================================================================
Status Impl::create_dummies() noexcept {
    @autoreleasepool {
        MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
        sd.minFilter = MTLSamplerMinMagFilterLinear;
        sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.mipFilter = MTLSamplerMipFilterNotMipmapped;
        sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.rAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.lodMaxClamp = 0.0f;
        sd.label = @"reserva-sampler";
        defaultSampler = [device newSamplerStateWithDescriptor:sd];
        if (!defaultSampler) return Status{Errc::OutOfDeviceMemory, "sampler padrao"};
    }

    TextureDesc d;
    d.width = d.height = 1;
    d.format = SurfaceFormat::RGBA8;
    d.sampled = true;
    d.transferDst = true;
    d.debugName = "reserva-amostrada";
    auto t = self->create_texture(d);
    if (!t.ok()) return t.status();
    dummyTexture = t->id;
    const u8 black[4] = {0, 0, 0, 0};
    if (const Status s = self->upload_texture(*t, black, 4); !s.ok()) return s;

    TextureDesc ds;
    ds.width = ds.height = 1;
    ds.format = SurfaceFormat::RGBA16F;
    ds.sampled = false;
    ds.storage = true;
    ds.debugName = "reserva-storage";
    auto st = self->create_texture(ds);
    if (!st.ok()) return st.status();
    dummyStorage = st->id;

    BufferDesc bd;
    bd.bytes = 256;
    bd.usage = BufferUsage::Storage;
    bd.debugName = "reserva-buffer";
    auto b = self->create_buffer(bd);
    if (!b.ok()) return b.status();
    dummyBuffer = b->id;
    return OkStatus;
}

// =============================================================================
// Submissão imediata: um command buffer próprio, submetido e esperado.
// É o caminho de quem roda fora do frame (import de 3D, upload de LUT, leitura
// de volta para os testes) — nunca o caminho do preview.
// =============================================================================
Status Impl::submit_immediate(void (*record)(Impl&, id<MTLCommandBuffer>, void*), void* ctx) noexcept {
    @autoreleasepool {
        if (!queue) return Status{Errc::InvalidState, "backend nao inicializado"};
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        if (!cb) return Status{Errc::InvalidState, "sem command buffer"};
        cb.label = @"imediato";
        dispatch_semaphore_t completion = command_buffer_completion(cb);
        if (!completion) return Status{Errc::OutOfMemory, "sem fence de command buffer"};
        record(*this, cb, ctx);
        [cb commit];
        const Status status = wait_command_buffer(cb, completion, 5'000'000'000ull, "submissao imediata");
        if (status.code() == Errc::Timeout) pendingImmediate.push_back(PendingSubmission{cb, completion});
        return status;
    }
}

// =============================================================================
// Fila de destruição adiada e espera de GPU
// =============================================================================
void Impl::run_deferred(FrameContext& f) noexcept {
    // Cópia antes de rodar: uma destruição pode, em cascata, adiar outra.
    std::vector<DeferredRelease> list;
    list.swap(f.deferred);
    for (const DeferredRelease& d : list) {
        if (d.fn) d.fn(d.ctx);
    }
    list.clear();
    if (f.deferred.empty()) f.deferred.swap(list);   // devolve a capacidade
}

void Impl::wait_frame_gpu(FrameContext& f) noexcept {
    if (!f.submitted) return;
    // Sem tempo limite aqui: quem chama já decidiu esperar (o `wait_frame` do
    // contrato é quem aceita tempo limite). Destruir/reciclar após um timeout
    // liberava IOSurfaces e sobrescrevia os anéis enquanto a GPU ainda os lia.
    const Status status = wait_command_buffer(f.cmd, f.completion, UINT64_MAX, "aguardar GPU ociosa");
    if (status.code() == Errc::DeviceLost) deviceLost = true;
}

void Impl::wait_immediate_gpu() noexcept {
    for (const PendingSubmission& pending : pendingImmediate) {
        (void)wait_command_buffer(pending.cmd, pending.completion, UINT64_MAX, "aguardar submissao imediata");
    }
    pendingImmediate.clear();
}

FrameContext* Impl::deferral_target() noexcept {
    if (current) return current;
    if (lastSubmitted && lastSubmitted->submitted) return lastSubmitted;
    return nullptr;
}

void Backend::defer_until_gpu_done(void (*fn)(void*), void* ctx) noexcept {
    if (!fn) return;
    Impl& d = *impl_;
    FrameContext* f = d.deferral_target();
    if (!f) {
        // Nenhum trabalho de GPU pendente: liberar agora é seguro. Um erro de
        // dispositivo não dispensa esperar outros CBs ainda em voo.
        fn(ctx);
        return;
    }
    f->deferred.push_back(DeferredRelease{fn, ctx});
}

void Backend::wait_idle() noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (!d.device) return;
        for (u32 i = 0; i < d.framesInFlight; ++i) {
            FrameContext& f = d.frames[i];
            if (&f == d.current || !f.submitted) continue;
            d.wait_frame_gpu(f);
            f.submitted = false;
            d.collect_timings(f);
            d.run_deferred(f);
        }
        d.wait_immediate_gpu();
    }
}

u64 Backend::last_submitted_frame() const noexcept {
    const Impl& d = *impl_;
    return d.lastSubmitted && d.lastSubmitted->submitted ? d.lastSubmitted->frameNumber : 0;
}

Status Backend::wait_frame(u64 frameNumber, u64 timeoutNs) noexcept {
    Impl& d = *impl_;
    if (!d.device) return Status{Errc::InvalidState, "backend nao inicializado"};
    if (d.deviceLost) return Status{Errc::DeviceLost, "dispositivo perdido"};
    for (u32 i = 0; i < d.framesInFlight; ++i) {
        FrameContext& f = d.frames[i];
        if (&f == d.current || !f.submitted || f.frameNumber != frameNumber) continue;
        // Só espera: coletar tempos e rodar a fila adiada continua com o
        // begin_frame que reciclar este contexto (uma thread só mexe nisso).
        return wait_command_buffer(f.cmd, f.completion, timeoutNs, "aguardar frame");
    }
    // Frame já reciclado = concluído (o begin_frame esperou seu command buffer).
    return OkStatus;
}

// =============================================================================
// Medição
// =============================================================================
void Impl::collect_timings(FrameContext& f) noexcept {
    if (!f.timersWritten || !f.counterBuffer || f.timerLabels.empty()) return;
    const u32 count = static_cast<u32>(f.timerLabels.size());
    const u32 used = 2 + count * 2;
    f.timersWritten = false;
    if (@available(iOS 14.0, macOS 11.0, *)) {
        @autoreleasepool {
            NSData* data = [f.counterBuffer resolveCounterRange:NSMakeRange(0, used)];
            if (!data || data.length < static_cast<NSUInteger>(used) * sizeof(MTLCounterResultTimestamp)) return;
            const MTLCounterResultTimestamp* ts =
                static_cast<const MTLCounterResultTimestamp*>(data.bytes);
            timings.clear();
            // O timestamp do Metal é em nanossegundos.
            constexpr f64 kToMs = 1e-6;
            for (u32 i = 0; i < count; ++i) {
                const u64 a = ts[2 + i * 2].timestamp;
                const u64 b = ts[2 + i * 2 + 1].timestamp;
                GpuTiming t;
                t.label = f.timerLabels[i];
                t.id = i;
                t.ms = b >= a ? static_cast<f32>(static_cast<f64>(b - a) * kToMs) : 0.0f;
                timings.push_back(t);
            }
            const u64 start = ts[0].timestamp;
            const u64 end = ts[1].timestamp;
            timingTotalMs = end >= start ? static_cast<f32>(static_cast<f64>(end - start) * kToMs) : 0.0f;
            timingsValid = true;
        }
    }
}

u32 Backend::read_gpu_timings(GpuTiming* out, u32 capacity, f32* totalMs) noexcept {
    const Impl& d = *impl_;
    if (!d.timingsValid || !out) return 0;
    const u32 n = std::min<u32>(capacity, static_cast<u32>(d.timings.size()));
    for (u32 i = 0; i < n; ++i) out[i] = d.timings[i];
    if (totalMs) *totalMs = d.timingTotalMs;
    return n;
}

GpuMemoryStats Backend::memory_stats() const noexcept {
    const Impl& d = *impl_;
    GpuMemoryStats s;
    // `currentAllocatedSize` é o que o driver já comprometeu com este processo —
    // o número real, medido por ele. Não há bloco/subalocação de nossa parte: o
    // Metal aloca por recurso, então `blockCount` fica 0 e as alocações são os
    // recursos vivos.
    s.reservedBytes = d.device ? static_cast<u64>(d.device.currentAllocatedSize) : 0;
    s.usedBytes = d.textureBytes + d.bufferBytes;
    s.blockCount = 0;
    s.allocationCount = d.allocationCount;
    s.textureCount = d.textures.count();
    s.bufferCount = d.buffers.count();
    s.uploadBytesThisFrame = d.uploadBytesFrame;
    return s;
}

// =============================================================================
// Cache de pipeline (MTLBinaryArchive)
//
// O equivalente honesto do `VkPipelineCache`: o `MTLBinaryArchive` guarda o
// binário já compilado de cada pipeline e o Metal reusa na abertura seguinte.
// O arquivo em disco é um CABEÇALHO NOSSO + o arquivo do archive:
//
//   magic "AUREAMTL" · formato · versão (impressão digital do MSL) · versão do
//   sistema · nome da GPU · tamanho · FNV-1a 64 do payload
//
// Motivo do cabeçalho: o Metal aceita o archive dele, mas um arquivo de OUTRA
// versão do sistema, de outra GPU ou de outro app é recusado com erro — e um
// erro de archive no meio da abertura é caro. Com o cabeçalho, o descarte é
// nosso, antes de entregar qualquer coisa ao driver.
//
// Se o `MTLBinaryArchive` não existir nesta versão do sistema (anterior ao
// iOS 14 / macOS 11), o backend NÃO mente: `pipeline_cache_info().load` fica
// `None` e `save_pipeline_cache` não grava.
// =============================================================================
namespace {

constexpr char kCacheMagic[8] = {'A', 'U', 'R', 'E', 'A', 'M', 'T', 'L'};
constexpr u32 kCacheFormat = 1;
constexpr usize kCacheMaxBytes = 64ull * 1024 * 1024;

struct CacheFileHeader {
    char magic[8];
    u32  format;
    u32  headerBytes;
    u64  tag;            ///< BackendConfig::pipelineCacheTag
    u32  osVersion;
    u32  reserved;
    u64  deviceHash;     ///< nome da GPU + memória unificada
    u64  payloadBytes;
    u64  checksum;
    u64  reservedTail;
};
static_assert(sizeof(CacheFileHeader) == 64, "cabecalho do cache com tamanho fixo");

u64 fnv1a(const u8* p, usize n) noexcept {
    u64 h = 1469598103934665603ull;
    for (usize i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

u64 device_hash(id<MTLDevice> dev) noexcept {
    NSString* name = dev.name ? dev.name : @"?";
    u64 h = fnv1a(reinterpret_cast<const u8*>(name.UTF8String), std::strlen(name.UTF8String));
    h ^= dev.hasUnifiedMemory ? 0x9E3779B97F4A7C15ull : 0;
    return h ? h : 1ull;
}

bool read_whole(const std::string& path, std::vector<u8>& out) noexcept {
    out.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool ok = false;
    if (std::fseek(f, 0, SEEK_END) == 0) {
        const long size = std::ftell(f);
        if (size > 0 && static_cast<usize>(size) <= kCacheMaxBytes + sizeof(CacheFileHeader)
            && std::fseek(f, 0, SEEK_SET) == 0) {
            out.resize(static_cast<usize>(size));
            ok = std::fread(out.data(), 1, out.size(), f) == out.size();
        }
    }
    std::fclose(f);
    if (!ok) out.clear();
    return ok;
}

bool write_whole(const std::string& path, const std::vector<u8>& bytes) noexcept {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    ok = std::fflush(f) == 0 && ok;
    ok = std::fclose(f) == 0 && ok;
    if (!ok) std::remove(path.c_str());
    return ok;
}

} // namespace

void Impl::load_pipeline_cache() noexcept {
    // `PipelineCacheInfo` é aninhado na interface: dentro de `Impl`, o nome
    // precisa do dono.
    using CacheInfo = GPUBackend::PipelineCacheInfo;
    cacheInfo = CacheInfo{};
    lastSavedCacheBytes = 0;
    archive = nil;
    if (!config.cacheDirectory || !*config.cacheDirectory) return;
    cachePath = std::string(config.cacheDirectory) + "/aurea_metal_pipeline_cache.bin";

    if (@available(iOS 14.0, macOS 11.0, *)) {
        @autoreleasepool {
            std::vector<u8> file;
            if (!read_whole(cachePath, file)) {
                cacheInfo.load = CacheInfo::Load::Missing;
                return;
            }
            if (file.size() < sizeof(CacheFileHeader)) {
                std::remove(cachePath.c_str());
                cacheInfo.load = CacheInfo::Load::Rejected;
                return;
            }
            CacheFileHeader h;
            std::memcpy(&h, file.data(), sizeof(h));
            const NSOperatingSystemVersion os = NSProcessInfo.processInfo.operatingSystemVersion;
            const u32 osVersion = static_cast<u32>((os.majorVersion << 16) | (os.minorVersion & 0xFFFF));
            const char* why = nullptr;
            if (std::memcmp(h.magic, kCacheMagic, sizeof(h.magic)) != 0) why = "formato antigo ou lixo";
            else if (h.format != kCacheFormat || h.headerBytes != sizeof(CacheFileHeader)) why = "formato de outra versao";
            else if (config.pipelineCacheTag != 0 && h.tag != config.pipelineCacheTag) why = "shaders mudaram (app atualizado)";
            else if (h.osVersion != osVersion) why = "sistema atualizado";
            else if (h.deviceHash != device_hash(device)) why = "outra GPU";
            else if (h.payloadBytes != file.size() - sizeof(CacheFileHeader)) why = "tamanho nao bate (truncado)";
            else if (fnv1a(file.data() + sizeof(CacheFileHeader), static_cast<usize>(h.payloadBytes)) != h.checksum) {
                why = "soma nao bate (corrompido)";
            }
            if (why) {
                AUREA_LOG_WARN("metal: cache de pipeline recusado (%s); recompilando", why);
                std::remove(cachePath.c_str());
                cacheInfo.load = CacheInfo::Load::Rejected;
                return;
            }

            // O payload é o ARQUIVO do archive, que o Metal só aceita por URL:
            // grava ao lado e entrega.
            const std::string payloadPath = cachePath + ".archive";
            std::vector<u8> payload(file.begin() + static_cast<std::ptrdiff_t>(sizeof(CacheFileHeader)), file.end());
            if (!write_whole(payloadPath, payload)) {
                cacheInfo.load = CacheInfo::Load::Missing;
                return;
            }
            MTLBinaryArchiveDescriptor* desc = [[MTLBinaryArchiveDescriptor alloc] init];
            desc.url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:payloadPath.c_str()]];
            NSError* err = nil;
            archive = [device newBinaryArchiveWithDescriptor:desc error:&err];
            std::remove(payloadPath.c_str());
            if (!archive) {
                (void)check_ns(err, "newBinaryArchiveWithDescriptor");
                AUREA_LOG_WARN("metal: driver recusou o cache de pipeline; recompilando");
                std::remove(cachePath.c_str());
                cacheInfo.load = CacheInfo::Load::Rejected;
                return;
            }
            cacheInfo.load = CacheInfo::Load::Loaded;
            cacheInfo.loadedBytes = payload.size();
            lastSavedCacheBytes = payload.size();
            AUREA_LOG_INFO("metal: cache de pipeline carregado (%zu KB)", payload.size() / 1024);
        }
    }
}

void Backend::save_pipeline_cache() noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (!d.device || !d.archive || d.cachePath.empty()) return;
        // Nada novo desde a carga: ir para segundo plano não regrava centenas de
        // KB à toa (o archive só cresce quando entra pipeline novo).
        if (d.pipelinesSinceSave == 0 && d.lastSavedCacheBytes != 0) return;
        if (@available(iOS 14.0, macOS 11.0, *)) {
            const std::string tmp = d.cachePath + ".tmp.archive";
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:tmp.c_str()]];
            NSError* err = nil;
            if (![d.archive serializeToURL:url error:&err]) {
                (void)check_ns(err, "MTLBinaryArchive serializeToURL");
                return;
            }
            std::vector<u8> payload;
            if (!read_whole(tmp, payload)) return;
            std::remove(tmp.c_str());
            if (payload.empty() || payload.size() > kCacheMaxBytes) {
                AUREA_LOG_WARN("metal: cache de pipeline grande demais (%zu KB); nao gravado", payload.size() / 1024);
                return;
            }
            CacheFileHeader h{};
            std::memcpy(h.magic, kCacheMagic, sizeof(h.magic));
            h.format = kCacheFormat;
            h.headerBytes = sizeof(CacheFileHeader);
            h.tag = d.config.pipelineCacheTag;
            const NSOperatingSystemVersion os = NSProcessInfo.processInfo.operatingSystemVersion;
            h.osVersion = static_cast<u32>((os.majorVersion << 16) | (os.minorVersion & 0xFFFF));
            h.deviceHash = device_hash(d.device);
            h.payloadBytes = payload.size();
            h.checksum = fnv1a(payload.data(), payload.size());

            std::vector<u8> file(sizeof(CacheFileHeader) + payload.size());
            std::memcpy(file.data(), &h, sizeof(h));
            std::memcpy(file.data() + sizeof(CacheFileHeader), payload.data(), payload.size());

            const std::string tmpFile = d.cachePath + ".tmp";
            if (!write_whole(tmpFile, file)) return;
            std::remove(d.cachePath.c_str());   // no POSIX rename substitui; no macOS também
            if (std::rename(tmpFile.c_str(), d.cachePath.c_str()) != 0) {
                std::remove(tmpFile.c_str());
                return;
            }
            d.lastSavedCacheBytes = payload.size();
            d.cacheInfo.savedBytes = payload.size();
            d.pipelinesSinceSave = 0;
            ++d.cacheInfo.saves;
        }
    }
}

Backend::PipelineCacheInfo Backend::pipeline_cache_info() const noexcept {
    return impl_->cacheInfo;
}

// =============================================================================
// Depuração de objetos
// =============================================================================
namespace {
void set_label(id object, const char* name) noexcept {
    if (!object || !name || !*name) return;
    @autoreleasepool {
        [object setLabel:[NSString stringWithUTF8String:name]];
    }
}
} // namespace
void set_object_label(id object, const char* name) noexcept { set_label(object, name); }

} // namespace aurea::mtl

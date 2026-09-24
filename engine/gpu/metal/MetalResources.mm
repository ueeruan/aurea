// =============================================================================
//  Recursos: texturas, buffers, samplers, shaders, pipelines, upload, leitura de
//  volta, mips e importação zero-copy do frame do decoder.
//
//  Diferenças estruturais frente ao Vulkan, todas por causa da API:
//
//   · Memória. Não há `vkAllocateMemory`: cada `MTLBuffer`/`MTLTexture` tem a
//     memória dele. O modo de armazenamento segue o uso declarado
//     (`MemoryAccess`): privado para a GPU, compartilhado para o que a CPU
//     escreve, e gerenciado só em Mac Intel (onde não há memória unificada).
//   · Não há render pass nem framebuffer como objeto: o `MTLRenderPassDescriptor`
//     é montado no começo de cada passe, com o load/store e o clear que o motor
//     pedir. Nada de cache de compatibilidade.
//   · Não há conjunto de descritores: o índice do recurso é o slot do motor.
// =============================================================================
#include "MetalInternal.hpp"

#include "aurea/core/Log.hpp"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <new>

namespace aurea::mtl {

namespace {

/// Destruição adiada: o objeto viaja num nó alocado até a GPU terminar o frame.
struct PendingTexture { Impl* impl; Texture texture; };
struct PendingBuffer { Impl* impl; Buffer buffer; };

/// Cabeçalho do blob MSL (ver `msl_glue.md`). O build embrulha o texto MSL ou o
/// `.metallib` nisto; o mesmo `ShaderBlob` que carrega SPIR-V no Android carrega
/// isto no iOS.
constexpr char kMslMagic[8] = {'A', 'U', 'R', 'E', 'A', 'M', 'S', 'L'};
struct MslHeader {
    char magic[8];
    u32  version;
    u32  flags;            ///< bit 0: o payload é um `.metallib` já compilado
    u32  payloadBytes;
    u32  threadgroup[3];   ///< compute: tamanho do grupo (0 = não declarado)
};
static_assert(sizeof(MslHeader) == 32, "cabecalho do MSL com tamanho fixo");
constexpr u32 kMslFlagMetallib = 1u;

bool looks_like_spirv(const u8* bytes, usize len) noexcept {
    if (len < 4) return false;
    u32 magic = 0;
    std::memcpy(&magic, bytes, 4);
    return magic == 0x07230203u;
}

const char* stage_suffix(ShaderStage stage) noexcept {
    switch (stage) {
        case ShaderStage::Vertex:   return "vertex_main";
        case ShaderStage::Fragment: return "fragment_main";
        case ShaderStage::Compute:  return "kernel_main";
    }
    return "main";
}

} // namespace

// =============================================================================
// Texturas
// =============================================================================
Result<TextureHandle> Backend::create_texture(const TextureDesc& desc) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (!d.device) return Status{Errc::InvalidState, "backend nao inicializado"};
        if (desc.width == 0 || desc.height == 0) return Status{Errc::InvalidArgument, "textura sem tamanho"};
        if (desc.width > d.caps.maxTexture2D || desc.height > d.caps.maxTexture2D) {
            return Status{Errc::OutOfRange, "textura maior que o limite do aparelho"};
        }
        if (desc.cube && (desc.width != desc.height || desc.layers != 6 || desc.depth != 1)) {
            return Status{Errc::InvalidArgument, "cubemap precisa de seis faces quadradas"};
        }

        Texture t;
        t.desc = desc;
        t.format = to_mtl(desc.format);
        t.state = ResourceState::Undefined;
        if (t.format == MTLPixelFormatInvalid) {
            return Status{Errc::UnsupportedFormat, "formato de textura indisponivel"};
        }

        MTLTextureDescriptor* td = [[MTLTextureDescriptor alloc] init];
        td.pixelFormat = t.format;
        td.width = desc.width;
        td.height = desc.height;
        td.mipmapLevelCount = std::max(1u, desc.mipLevels);
        // Metal conta cubos, não faces, em arrayLength. Um MTLTextureTypeCube
        // tem arrayLength=1 e seis slices (0...5) para upload e amostragem.
        td.arrayLength = desc.cube ? 1u : std::max(1u, desc.layers);
        // O motor é 1 amostra em todo alvo (o AA do preview é resolução, não
        // MSAA): declarado e não usado. Um dia que entre, entra aqui.
        td.sampleCount = 1;
        // Textura do motor é sempre privada: a CPU sobe por staging e lê por
        // blit. É o mesmo caminho do Vulkan (DEVICE_LOCAL + transfer).
        td.storageMode = MTLStorageModePrivate;
        td.usage = MTLTextureUsageShaderRead;
        if (desc.cube) {
            td.textureType = MTLTextureTypeCube;
        } else if (desc.depth > 1) {
            td.textureType = MTLTextureType3D;
            td.depth = desc.depth;
        } else if (td.arrayLength > 1) {
            td.textureType = MTLTextureType2DArray;
        } else {
            td.textureType = MTLTextureType2D;
        }
        if (desc.storage) td.usage |= MTLTextureUsageShaderWrite;
        if (desc.renderTarget || desc.is_depth()) td.usage |= MTLTextureUsageRenderTarget;

        t.texture = [d.device newTextureWithDescriptor:td];
        if (!t.texture) {
            // Formato que esta GPU não cria (BC numa GPU Apple, por exemplo: o
            // transcodificador do KTX2 precisa escolher ASTC no iOS).
            AUREA_LOG_ERROR("metal: formato de textura indisponivel nesta GPU (%d)", static_cast<int>(t.format));
            return Status{Errc::UnsupportedFormat, "formato de textura indisponivel"};
        }
        set_object_label(t.texture, desc.debugName);
        t.lastUsedFrame = d.frameNumber;
        d.textureBytes += desc.estimated_bytes();
        ++d.allocationCount;
        return TextureHandle{d.textures.add(std::move(t))};
    }
}

void Impl::destroy_texture_now(Texture& t) noexcept {
    if (t.external) {
        // Refs de CoreVideo são CF: ARC não os toca.
        if (t.pixelBuffer) {
            CVPixelBufferRelease(t.pixelBuffer);
            t.pixelBuffer = nullptr;
        }
        if (t.cvTexture) {
            CFRelease(t.cvTexture);
            t.cvTexture = nullptr;
        }
    }
    const u64 bytes = t.desc.estimated_bytes();
    textureBytes -= std::min<u64>(textureBytes, bytes);
    if (allocationCount) --allocationCount;
    t.texture = nil;
    t = Texture{};
}

void Backend::destroy_texture(TextureHandle h) noexcept {
    Impl& d = *impl_;
    Texture t;
    if (!d.textures.remove(h.id, t)) return;
    if (t.external && t.pixelBuffer) {
        for (auto it = d.importedByBuffer.begin(); it != d.importedByBuffer.end(); ++it) {
            if (it->second == h.id) {
                d.importedByBuffer.erase(it);
                break;
            }
        }
    }
    auto* pending = new (std::nothrow) PendingTexture{&d, std::move(t)};
    if (!pending) return;
    defer_until_gpu_done([](void* p) {
        auto* node = static_cast<PendingTexture*>(p);
        node->impl->destroy_texture_now(node->texture);
        delete node;
    }, pending);
}

TextureDesc Backend::texture_desc(TextureHandle h) const noexcept {
    const Texture* t = impl_->textures.get(h.id);
    return t ? t->desc : TextureDesc{};
}

// =============================================================================
// Buffers
// =============================================================================
Result<BufferHandle> Backend::create_buffer(const BufferDesc& desc) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (!d.device) return Status{Errc::InvalidState, "backend nao inicializado"};
        if (desc.bytes == 0) return Status{Errc::InvalidArgument, "buffer vazio"};

        Buffer b;
        b.desc = desc;
        MTLResourceOptions options = MTLResourceStorageModePrivate;
        switch (desc.access) {
            case MemoryAccess::GpuOnly:
                options = MTLResourceStorageModePrivate;
                break;
            case MemoryAccess::Upload:
                // Memória unificada: a CPU escreve no mesmo endereço que a GPU lê.
                options = MTLResourceStorageModeShared;
                break;
            case MemoryAccess::Readback:
                // `Managed` só existe em Mac Intel (não há memória unificada) —
                // e lá a leitura de volta depois de um blit precisa de
                // `synchronizeResource:`, feito no caminho da leitura.
#if TARGET_OS_OSX
                options = d.device.hasUnifiedMemory ? MTLResourceStorageModeShared
                                                    : MTLResourceStorageModeManaged;
#else
                options = MTLResourceStorageModeShared;
#endif
                break;
        }
#if TARGET_OS_OSX
        b.managed = (options == MTLResourceStorageModeManaged);
#else
        b.managed = false;
#endif
        b.hostVisible = (options != MTLResourceStorageModePrivate);
        b.buffer = [d.device newBufferWithLength:desc.bytes options:options];
        if (!b.buffer) return Status{Errc::OutOfDeviceMemory, "sem memoria para buffer"};
        set_object_label(b.buffer, desc.debugName);
        d.bufferBytes += desc.bytes;
        ++d.allocationCount;
        return BufferHandle{d.buffers.add(std::move(b))};
    }
}

void Impl::destroy_buffer_now(Buffer& b) noexcept {
    bufferBytes -= std::min<u64>(bufferBytes, b.desc.bytes);
    if (allocationCount) --allocationCount;
    b.buffer = nil;
    b = Buffer{};
}

void Backend::destroy_buffer(BufferHandle h) noexcept {
    Impl& d = *impl_;
    Buffer b;
    if (!d.buffers.remove(h.id, b)) return;
    auto* pending = new (std::nothrow) PendingBuffer{&d, std::move(b)};
    if (!pending) return;
    defer_until_gpu_done([](void* p) {
        auto* node = static_cast<PendingBuffer*>(p);
        node->impl->destroy_buffer_now(node->buffer);
        delete node;
    }, pending);
}

Status Backend::write_buffer(BufferHandle dst, usize offset, const void* data, usize bytes) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        Buffer* b = d.buffers.get(dst.id);
        if (!b || !data) return Errc::InvalidArgument;
        if (offset + bytes > b->desc.bytes) return Errc::OutOfRange;
        if (b->hostVisible) {
            std::memcpy(static_cast<u8*>(b->buffer.contents) + offset, data, bytes);
#if TARGET_OS_OSX
            if (b->managed) [b->buffer didModifyRange:NSMakeRange(offset, bytes)];
#endif
            return OkStatus;
        }
        // Buffer só de GPU: staging + blit, síncrono (não é caminho de frame).
        // O MTLBuffer de destino é copiado ANTES de criar o staging: `create_buffer`
        // pode realocar o pool e `b` ficaria pendurado (o mesmo use-after-free
        // que a Fase 8 achou no caminho Vulkan).
        id<MTLBuffer> target = b->buffer;
        b = nullptr;
        BufferDesc sd;
        sd.bytes = bytes;
        sd.usage = BufferUsage::TransferSrc;
        sd.access = MemoryAccess::Upload;
        sd.debugName = "staging-buffer";
        auto staging = create_buffer(sd);
        if (!staging.ok()) return staging.status();
        Buffer* s = d.buffers.get(staging->id);
        if (!s) return Errc::InvalidState;
        std::memcpy(s->buffer.contents, data, bytes);
#if TARGET_OS_OSX
        if (s->managed) [s->buffer didModifyRange:NSMakeRange(0, bytes)];
#endif
        id<MTLBuffer> source = s->buffer;
        struct Ctx { id<MTLBuffer> src; id<MTLBuffer> dst; usize offset; usize size; }
            ctx{source, target, offset, bytes};
        const Status st = d.submit_immediate([](Impl&, id<MTLCommandBuffer> cb, void* p) {
            auto* c = static_cast<Ctx*>(p);
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit copyFromBuffer:c->src sourceOffset:0 toBuffer:c->dst destinationOffset:c->offset size:c->size];
            [blit endEncoding];
        }, &ctx);
        Buffer dead;
        if (d.buffers.remove(staging->id, dead)) d.destroy_buffer_now(dead);
        return st;
    }
}

Status Backend::map_buffer(BufferHandle buffer, void*& outPtr) noexcept {
    Impl& d = *impl_;
    Buffer* b = d.buffers.get(buffer.id);
    if (!b || !b->hostVisible) return Errc::InvalidArgument;
    outPtr = b->buffer.contents;
    return OkStatus;
}

void Backend::unmap_buffer(BufferHandle buffer) noexcept {
    Impl& d = *impl_;
    Buffer* b = d.buffers.get(buffer.id);
    if (!b || !b->managed) return;
    // `Managed` no Mac Intel: sem isto a escrita da CPU não chega à GPU.
#if TARGET_OS_OSX
    [b->buffer didModifyRange:NSMakeRange(0, b->desc.bytes)];
#endif
}

// =============================================================================
// Samplers
// =============================================================================
Result<SamplerHandle> Backend::create_sampler(const SamplerDesc& desc) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (!d.device) return Status{Errc::InvalidState, "backend nao inicializado"};
        MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
        sd.minFilter = desc.minFilter == SamplerDesc::Filter::Linear ? MTLSamplerMinMagFilterLinear
                                                                     : MTLSamplerMinMagFilterNearest;
        sd.magFilter = desc.magFilter == SamplerDesc::Filter::Linear ? MTLSamplerMinMagFilterLinear
                                                                     : MTLSamplerMinMagFilterNearest;
        sd.mipFilter = desc.mipmap == SamplerDesc::Mipmap::Linear ? MTLSamplerMipFilterLinear
                                                                  : MTLSamplerMipFilterNearest;
        sd.sAddressMode = to_mtl(desc.wrapU);
        sd.tAddressMode = to_mtl(desc.wrapV);
        sd.rAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.lodMinClamp = 0.0f;
        sd.lodMaxClamp = FLT_MAX;
        if (desc.wrapU == SamplerDesc::Wrap::ClampToBorder || desc.wrapV == SamplerDesc::Wrap::ClampToBorder) {
            if (@available(iOS 14.0, macOS 10.12, *)) {
                // Borda transparente: é o que efeito de vizinhança espera para não
                // esticar a última coluna da imagem.
                sd.borderColor = MTLSamplerBorderColorTransparentBlack;
            } else {
                sd.sAddressMode = desc.wrapU == SamplerDesc::Wrap::ClampToBorder ? MTLSamplerAddressModeClampToEdge
                                                                                 : sd.sAddressMode;
                sd.tAddressMode = desc.wrapV == SamplerDesc::Wrap::ClampToBorder ? MTLSamplerAddressModeClampToEdge
                                                                                 : sd.tAddressMode;
                AUREA_LOG_WARN("metal: ClampToBorder sem suporte nesta versao; usando ClampToEdge");
            }
        }
        if (desc.maxAnisotropy > 1.0f) {
            sd.maxAnisotropy = std::min<NSUInteger>(static_cast<NSUInteger>(desc.maxAnisotropy), 16);
        }
        SamplerObject s;
        s.sampler = [d.device newSamplerStateWithDescriptor:sd];
        if (!s.sampler) return Status{Errc::OutOfDeviceMemory, "sem sampler"};
        return SamplerHandle{d.samplers.add(std::move(s))};
    }
}

void Backend::destroy_sampler(SamplerHandle h) noexcept {
    Impl& d = *impl_;
    if (!d.samplers.get(h.id)) return;
    SamplerObject dead;
    d.samplers.remove(h.id, dead);
    struct Node { id<MTLSamplerState> sampler; };
    auto* node = new (std::nothrow) Node{dead.sampler};
    if (!node) return;
    defer_until_gpu_done([](void* p) {
        auto* n = static_cast<Node*>(p);
        n->sampler = nil;
        delete n;
    }, node);
}

// =============================================================================
// Shaders (MSL embutido no build — ver msl_glue.md)
// =============================================================================
Result<ShaderHandle> Backend::create_shader(const ShaderDesc& desc) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (!d.device) return Status{Errc::InvalidState, "backend nao inicializado"};
        if (!desc.spirv || desc.spirvBytes < 8) return Status{Errc::InvalidArgument, "blob de shader vazio"};

        const u8* bytes = reinterpret_cast<const u8*>(desc.spirv);
        if (looks_like_spirv(bytes, desc.spirvBytes)) {
            // O motor embutiu SPIR-V: no iOS o blob tem de ser MSL. Falha clara em
            // vez de compilar lixo.
            AUREA_LOG_ERROR("metal: '%s' veio como SPIR-V; o build do iOS precisa embutir o MSL",
                            desc.debugName ? desc.debugName : "?");
            return Status{Errc::ShaderCompileFailed, "blob SPIR-V em backend Metal"};
        }

        usize headerBytes = 0;
        usize payloadBytes = desc.spirvBytes;
        bool metallib = false;
        u32 threadgroup[3] = {0, 0, 0};
        if (desc.spirvBytes >= sizeof(MslHeader) && std::memcmp(bytes, kMslMagic, sizeof(kMslMagic)) == 0) {
            MslHeader h{};
            std::memcpy(&h, bytes, sizeof(h));
            if (h.version != 1) return Status{Errc::UnsupportedVersion, "versao do cabecalho MSL"};
            if (h.payloadBytes == 0 || h.payloadBytes > desc.spirvBytes - sizeof(MslHeader)) {
                return Status{Errc::CorruptData, "tamanho do payload MSL"};
            }
            headerBytes = sizeof(MslHeader);
            // O comprimento é o DECLARADO: o blob vem alinhado em palavras de
            // 32 bits, e o preenchimento que sobra não faz parte do shader.
            payloadBytes = h.payloadBytes;
            metallib = (h.flags & kMslFlagMetallib) != 0;
            threadgroup[0] = h.threadgroup[0];
            threadgroup[1] = h.threadgroup[1];
            threadgroup[2] = h.threadgroup[2];
        }

        ShaderObject s;
        s.stage = desc.stage;
        NSError* err = nil;
        if (metallib) {
            // O blob esta embutido no executavel e permanece valido durante toda
            // a vida do processo; libdispatch nao deve liberar essa memoria.
            dispatch_data_t data = dispatch_data_create(bytes + headerBytes, payloadBytes, nil,
                                                        ^{});
            if (!data) return Status{Errc::OutOfMemory, "dispatch_data do metallib"};
            s.library = [d.device newLibraryWithData:data error:&err];
        } else {
            // Texto MSL: um `#include <metal_stdlib>` + `using namespace metal;`
            // como o SPIRV-Cross emite. A compilação acontece aqui, uma vez por
            // shader, na abertura — nunca no meio do playback.
            NSString* source = [[NSString alloc] initWithBytes:bytes + headerBytes
                                                        length:payloadBytes
                                                      encoding:NSUTF8StringEncoding];
            if (!source) return Status{Errc::InvalidArgument, "MSL nao e UTF-8"};
            MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
            s.library = [d.device newLibraryWithSource:source options:options error:&err];
        }
        if (!s.library) {
            const Status st = check_ns(err, "biblioteca MSL");
            AUREA_LOG_ERROR("metal: shader '%s' nao compilou", desc.debugName ? desc.debugName : "?");
            return Status{Errc::ShaderCompileFailed, st.detail().data()};
        }
        set_object_label(s.library, desc.debugName);

        // Nome da entrada: o estágio manda (vs_main/fs_main/cs_main) e um
        // `entryPoint` explícito no descritor vence — é o que deixa um shader
        // escrito à mão usar o próprio nome.
        const char* wanted = (desc.entryPoint && std::strcmp(desc.entryPoint, "main") != 0)
                           ? desc.entryPoint : stage_entry_point(desc.stage);
        s.function = [s.library newFunctionWithName:[NSString stringWithUTF8String:wanted]];
        if (!s.function) {
            // Convenção não seguida pelo build: aceita o nome que o SPIRV-Cross
            // emite por padrão, avisando — em vez de derrubar a abertura por um
            // detalhe de script.
            const char* fallback = stage_suffix(desc.stage);
            s.function = [s.library newFunctionWithName:[NSString stringWithUTF8String:fallback]];
            if (s.function) {
                AUREA_LOG_WARN("metal: '%s' nao define '%s'; usando '%s' (ajuste o build)",
                               desc.debugName ? desc.debugName : "?", wanted, fallback);
            }
        }
        if (!s.function) {
            AUREA_LOG_ERROR("metal: '%s' nao exporta '%s' (funcoes na biblioteca:)",
                            desc.debugName ? desc.debugName : "?", wanted);
            AUREA_LOG_ERROR("metal: %s", s.library.functionNames.description.UTF8String);
            return Status{Errc::ShaderCompileFailed, "funcao de entrada ausente"};
        }

        if (desc.stage == ShaderStage::Compute) {
            if (threadgroup[0] == 0) {
                // Sem o tamanho do grupo o `dispatch` não existe: o Metal não o
                // conhece pelo pipeline. O build declara no cabeçalho do blob.
                AUREA_LOG_ERROR("metal: compute '%s' sem tamanho de grupo no blob (ver msl_glue.md)",
                                desc.debugName ? desc.debugName : "?");
                return Status{Errc::InvalidArgument, "tamanho do grupo de threads ausente"};
            }
            s.threadgroup[0] = threadgroup[0];
            s.threadgroup[1] = threadgroup[1] ? threadgroup[1] : 1u;
            s.threadgroup[2] = threadgroup[2] ? threadgroup[2] : 1u;
        }
        return ShaderHandle{d.shaders.add(std::move(s))};
    }
}

void Backend::destroy_shader(ShaderHandle h) noexcept {
    Impl& d = *impl_;
    ShaderObject s;
    if (!d.shaders.remove(h.id, s)) return;
    // A `MTLFunction`/`MTLLibrary` de um pipeline já criado não é mais
    // necessária, mas a destruição vai adiada por simetria com o Vulkan (um
    // shader pode ser destruído logo depois de criar os pipelines que o usam).
    struct Node { id<MTLLibrary> library; id<MTLFunction> function; };
    auto* node = new (std::nothrow) Node{s.library, s.function};
    if (!node) return;
    defer_until_gpu_done([](void* p) {
        auto* n = static_cast<Node*>(p);
        n->function = nil;
        n->library = nil;
        delete n;
    }, node);
}

// =============================================================================
// Estado de profundidade
// =============================================================================
id<MTLDepthStencilState> Impl::depth_state(const PipelineDesc& desc) noexcept {
    const u64 key = static_cast<u64>(desc.depth.test ? 1u : 0u) | (static_cast<u64>(desc.depth.write ? 1u : 0u) << 1)
                  | (static_cast<u64>(desc.depth.compare) << 2);
    if (auto it = depthStates.find(key); it != depthStates.end()) return it->second;
    @autoreleasepool {
        MTLDepthStencilDescriptor* dd = [[MTLDepthStencilDescriptor alloc] init];
        dd.depthCompareFunction = desc.depth.test ? to_mtl(desc.depth.compare) : MTLCompareFunctionAlways;
        dd.depthWriteEnabled = desc.depth.write ? YES : NO;
        id<MTLDepthStencilState> state = [device newDepthStencilStateWithDescriptor:dd];
        if (state) depthStates.emplace(key, state);
        return state;
    }
}

// =============================================================================
// Pipelines
// =============================================================================
Result<PipelineHandle> Backend::create_pipeline(const PipelineDesc& desc) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (!d.device) return Status{Errc::InvalidState, "backend nao inicializado"};
        PipelineObject p;
        p.isCompute = desc.isCompute;
        p.cull = desc.cull == CullMode::Back ? MTLCullModeBack
               : desc.cull == CullMode::Front ? MTLCullModeFront : MTLCullModeNone;
        p.winding = desc.frontFaceCCW ? MTLWindingCounterClockwise : MTLWindingClockwise;
        p.topology = to_mtl(desc.topology);
        if (desc.depth.biasConstant != 0.0f || desc.depth.biasSlope != 0.0f) {
            p.depthBiasEnabled = true;
            p.depthBias = desc.depth.biasConstant;
            p.depthBiasSlope = desc.depth.biasSlope;
        }
        NSError* err = nil;

        if (desc.isCompute) {
            ShaderObject* cs = d.shaders.get(desc.computeShader.id);
            if (!cs || !cs->function) return Status{Errc::InvalidArgument, "shader de compute ausente"};
            MTLComputePipelineDescriptor* cd = [[MTLComputePipelineDescriptor alloc] init];
            cd.computeFunction = cs->function;
            cd.label = desc.debugName ? [NSString stringWithUTF8String:desc.debugName] : @"compute";
            if (@available(iOS 14.0, macOS 11.0, *)) {
                if (d.archive) cd.binaryArchives = @[ d.archive ];
            }
            p.compute = [d.device newComputePipelineStateWithDescriptor:cd
                                                                options:MTLPipelineOptionNone
                                                              reflection:nil
                                                                   error:&err];
            if (!p.compute) {
                (void)check_ns(err, "newComputePipelineStateWithDescriptor");
                return Status{Errc::PipelineCompileFailed, desc.debugName};
            }
            // O tamanho do grupo vem do shader (o Metal não o tem no pipeline).
            p.threadgroup[0] = cs->threadgroup[0];
            p.threadgroup[1] = cs->threadgroup[1];
            p.threadgroup[2] = cs->threadgroup[2];
            ++d.pipelinesSinceSave;
            return PipelineHandle{d.pipelines.add(std::move(p))};
        }

        ShaderObject* vs = d.shaders.get(desc.vertexShader.id);
        ShaderObject* fs = d.shaders.get(desc.fragmentShader.id);
        if (!vs || !fs || !vs->function || !fs->function) {
            return Status{Errc::InvalidArgument, "shader grafico ausente"};
        }
        MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
        pd.vertexFunction = vs->function;
        pd.fragmentFunction = fs->function;
        pd.label = desc.debugName ? [NSString stringWithUTF8String:desc.debugName] : @"render";
        pd.rasterSampleCount = 1;
        pd.inputPrimitiveTopology = to_mtl_topology(desc.topology);
        // Alvo da cor: um pipeline só de profundidade (sombra) não tem anexo de
        // cor, e o formato TEM de ser inválido no descritor — senão o Metal
        // recusa o draw no passe sem cor.
        pd.colorAttachments[0].pixelFormat = desc.depthOnly ? MTLPixelFormatInvalid : to_mtl(desc.colorFormat);
        if (desc.hasDepth) {
            pd.depthAttachmentPixelFormat = to_mtl(desc.depthFormat);
        } else {
            pd.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
        }
        // Alvos PRÉ-MULTIPLICADOS: Normal = src + dst*(1-srcA); Add = src + dst.
        if (desc.blendEnabled) {
            MTLRenderPipelineColorAttachmentDescriptor* a = pd.colorAttachments[0];
            a.blendingEnabled = YES;
            a.sourceRGBBlendFactor = MTLBlendFactorOne;
            a.sourceAlphaBlendFactor = MTLBlendFactorOne;
            a.rgbBlendOperation = MTLBlendOperationAdd;
            a.alphaBlendOperation = MTLBlendOperationAdd;
            a.destinationRGBBlendFactor = desc.blend == BlendMode::Add ? MTLBlendFactorOne
                                                                       : MTLBlendFactorOneMinusSourceAlpha;
            a.destinationAlphaBlendFactor = desc.blend == BlendMode::Add ? MTLBlendFactorOne
                                                                         : MTLBlendFactorOneMinusSourceAlpha;
        }
        pd.colorAttachments[0].writeMask = MTLColorWriteMaskAll;

        MTLVertexDescriptor* vd = nil;
        const VertexLayout& vl = desc.vertexLayout;
        if (vl.attributeCount > 0) {
            vd = [[MTLVertexDescriptor alloc] init];
            for (u32 i = 0; i < vl.attributeCount && i < VertexLayout::kMaxAttributes; ++i) {
                const VertexAttribute& attr = vl.attributes[i];
                const NSUInteger loc = attr.location;
                if (loc >= 31) continue;   // limite de atributos do Metal
                vd.attributes[loc].format = to_mtl(attr.format);
                vd.attributes[loc].offset = attr.offset;
                vd.attributes[loc].bufferIndex = attr.binding;
            }
            for (u32 b = 0; b < vl.bindingCount && b < VertexLayout::kMaxBindings; ++b) {
                if (vl.bindings[b].stride == 0) continue;
                vd.layouts[b].stride = vl.bindings[b].stride;
                vd.layouts[b].stepFunction = vl.bindings[b].perInstance ? MTLVertexStepFunctionPerInstance
                                                                        : MTLVertexStepFunctionPerVertex;
                vd.layouts[b].stepRate = 1;
            }
        }
        pd.vertexDescriptor = vd;
        if (@available(iOS 14.0, macOS 11.0, *)) {
            if (d.archive) pd.binaryArchives = @[ d.archive ];
        }

        p.render = [d.device newRenderPipelineStateWithDescriptor:pd error:&err];
        if (!p.render) {
            (void)check_ns(err, "newRenderPipelineStateWithDescriptor");
            return Status{Errc::PipelineCompileFailed, desc.debugName};
        }
        if (desc.hasDepth) {
            p.depth = d.depth_state(desc);
            if (!p.depth) return Status{Errc::PipelineCompileFailed, "estado de profundidade"};
        }
        ++d.pipelinesSinceSave;
        return PipelineHandle{d.pipelines.add(std::move(p))};
    }
}

void Backend::destroy_pipeline(PipelineHandle h) noexcept {
    Impl& d = *impl_;
    PipelineObject p;
    if (!d.pipelines.remove(h.id, p)) return;
    struct Node { id<MTLRenderPipelineState> render; id<MTLComputePipelineState> compute; id<MTLDepthStencilState> depth; };
    auto* node = new (std::nothrow) Node{p.render, p.compute, p.depth};
    if (!node) return;
    defer_until_gpu_done([](void* ptr) {
        auto* n = static_cast<Node*>(ptr);
        n->render = nil;
        n->compute = nil;
        n->depth = nil;
        delete n;
    }, node);
}

// =============================================================================
// Upload e leitura de volta
// =============================================================================
Status Backend::upload_texture(TextureHandle dst, const void* data, u32 bytesPerRow) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        Texture* t = d.textures.get(dst.id);
        if (!t || !data) return Errc::InvalidArgument;
        const u32 bpp = t->desc.bytes_per_pixel();
        const u32 w = t->desc.width;
        const u32 h = t->desc.height;
        if (bpp == 0) return Status{Errc::InvalidArgument, "formato sem bytes por texel"};
        const u32 rowBytes = w * bpp;
        const u32 pitch = bytesPerRow ? bytesPerRow : rowBytes;
        if (pitch < rowBytes || pitch % bpp != 0) return Status{Errc::InvalidArgument, "passo de linha invalido"};
        const usize total = static_cast<usize>(rowBytes) * h;
        id<MTLTexture> texture = t->texture;
        if (!texture) return Errc::InvalidState;

        struct Ctx {
            id<MTLTexture> dst = nil;
            id<MTLBuffer>  src = nil;
            u32  w = 0, h = 0, rowBytes = 0;
            usize total = 0;
            u32  sourceOffset = 0;
            bool managed = false;
        };
        // A cópia é sempre a mesma; só muda a origem (anel do frame ou staging
        // dedicado). Uma função só evita duas versões da mesma conta.
        const auto copy = [](id<MTLCommandBuffer> cb, void* p) {
            auto* c = static_cast<Ctx*>(p);
#if TARGET_OS_OSX
            if (c->managed) [c->src didModifyRange:NSMakeRange(0, c->total)];
#endif
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit copyFromBuffer:c->src
                    sourceOffset:c->sourceOffset
                sourceBytesPerRow:c->rowBytes
              sourceBytesPerImage:0
                       sourceSize:MTLSizeMake(c->w, c->h, 1)
                        toTexture:c->dst
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
        };

        Ctx ctx;
        ctx.dst = texture;
        ctx.w = w;
        ctx.h = h;
        ctx.rowBytes = rowBytes;
        ctx.total = total;

        if (d.current && !d.commands.in_render_pass()) {
            // Frame aberto: o staging entra no anel do frame e a cópia vai para o
            // command buffer atual, ANTES dos passes que leem a textura. O blit
            // precisa de um encoder só dele — em Metal um encoder por vez.
            d.commands.finish_encoders();
            id<MTLBuffer> buffer = nil;
            u32 offset = 0;
            void* ptr = nullptr;
            if (!d.current->staging.allocate(total, 16, buffer, offset, ptr)) return Errc::OutOfMemory;
            const u8* src = static_cast<const u8*>(data);
            u8* out = static_cast<u8*>(ptr);
            if (pitch == rowBytes) {
                std::memcpy(out, src, total);
            } else {
                for (u32 y = 0; y < h; ++y) {
                    std::memcpy(out + static_cast<usize>(y) * rowBytes, src + static_cast<usize>(y) * pitch, rowBytes);
                }
            }
            ctx.src = buffer;
            ctx.sourceOffset = offset;
            copy(d.current->cmd, &ctx);
            d.uploadBytesFrame += total;
            return OkStatus;
        }

        // Fora de frame: staging próprio, submissão e espera.
        BufferDesc sd;
        sd.bytes = total;
        sd.usage = BufferUsage::TransferSrc;
        sd.access = MemoryAccess::Upload;
        sd.debugName = "staging-upload";
        auto staging = create_buffer(sd);
        if (!staging.ok()) return staging.status();
        Buffer* s = d.buffers.get(staging->id);
        if (!s) return Errc::InvalidState;
        const u8* src = static_cast<const u8*>(data);
        u8* out = static_cast<u8*>(s->buffer.contents);
        for (u32 y = 0; y < h; ++y) {
            std::memcpy(out + static_cast<usize>(y) * rowBytes, src + static_cast<usize>(y) * pitch, rowBytes);
        }
        ctx.src = s->buffer;
        ctx.managed = s->managed;
        const Status st = d.submit_immediate([](Impl&, id<MTLCommandBuffer> cb, void* p) {
            auto* c = static_cast<Ctx*>(p);
#if TARGET_OS_OSX
            if (c->managed) [c->src didModifyRange:NSMakeRange(0, c->total)];
#endif
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit copyFromBuffer:c->src
                    sourceOffset:0
                sourceBytesPerRow:c->rowBytes
              sourceBytesPerImage:0
                       sourceSize:MTLSizeMake(c->w, c->h, 1)
                        toTexture:c->dst
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
        }, &ctx);
        Buffer dead;
        if (d.buffers.remove(staging->id, dead)) d.destroy_buffer_now(dead);
        return st;
    }
}

Status Backend::upload_texture_level(TextureHandle dst, u32 mipLevel, u32 layer, const void* data,
                                     usize bytes) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        Texture* t = d.textures.get(dst.id);
        if (!t || !data || bytes == 0) return Errc::InvalidArgument;
        const u32 levels = std::max(1u, t->desc.mipLevels);
        const u32 layers = t->desc.cube ? 6u : std::max(1u, t->desc.layers);
        if (mipLevel >= levels || layer >= layers) return Status{Errc::OutOfRange, "mip/camada fora da textura"};
        const u32 w = std::max(1u, t->desc.width >> mipLevel);
        const u32 h = std::max(1u, t->desc.height >> mipLevel);
        const bool block = is_block_compressed(t->desc.format);
        // Linhas do nível: bloco de 4×4 para comprimido (16 bytes), texel cheio
        // para o resto. O Metal exige o passo de linha múltiplo do bloco.
        const u32 rows = block ? (h + 3) / 4 : h;
        const u32 rowBytes = block ? ((w + 3) / 4) * 16 : w * t->desc.bytes_per_pixel();
        const usize expected = static_cast<usize>(rowBytes) * rows;
        if (bytes < expected) return Status{Errc::InvalidArgument, "dados menores que o nivel"};
        id<MTLTexture> texture = t->texture;
        if (!texture) return Errc::InvalidState;

        // Sempre por submissão própria: o import de 3D roda fora do frame, e um
        // nível 4K não cabe no anel de staging do frame.
        BufferDesc sd;
        sd.bytes = expected;
        sd.usage = BufferUsage::TransferSrc;
        sd.access = MemoryAccess::Upload;
        sd.debugName = "staging-nivel";
        auto staging = create_buffer(sd);
        if (!staging.ok()) return staging.status();
        Buffer* s = d.buffers.get(staging->id);
        if (!s) return Errc::InvalidState;
        std::memcpy(s->buffer.contents, data, expected);
#if TARGET_OS_OSX
        if (s->managed) [s->buffer didModifyRange:NSMakeRange(0, expected)];
#endif

        struct Ctx { id<MTLTexture> dst; id<MTLBuffer> src; u32 mip, layer, w, h, rowBytes; usize total; }
            ctx{texture, s->buffer, mipLevel, layer, w, h, rowBytes, expected};
        const Status st = d.submit_immediate([](Impl&, id<MTLCommandBuffer> cb, void* p) {
            auto* c = static_cast<Ctx*>(p);
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit copyFromBuffer:c->src
                    sourceOffset:0
                sourceBytesPerRow:c->rowBytes
              sourceBytesPerImage:0
                       sourceSize:MTLSizeMake(c->w, c->h, 1)
                        toTexture:c->dst
                 destinationSlice:c->layer
                 destinationLevel:c->mip
                destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
        }, &ctx);
        Buffer dead;
        if (d.buffers.remove(staging->id, dead)) d.destroy_buffer_now(dead);
        if (st.ok()) t->state = ResourceState::ShaderRead;
        return st;
    }
}

Status Backend::generate_mipmaps(TextureHandle texture) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        Texture* t = d.textures.get(texture.id);
        if (!t) return Errc::InvalidArgument;
        const u32 levels = std::max(1u, t->desc.mipLevels);
        if (levels <= 1) return OkStatus;
        if (is_block_compressed(t->desc.format) || t->desc.is_depth()) {
            return Status{Errc::NotSupported, "mips deste formato vem do arquivo"};
        }
        id<MTLTexture> tex = t->texture;
        if (!tex) return Errc::InvalidState;
        // `generateMipmapsForTexture:` é o blit de mip chain do próprio Metal:
        // mesma conta da cadeia de `vkCmdBlitImage` do Vulkan, feita pela GPU.
        const Status st = d.submit_immediate([](Impl&, id<MTLCommandBuffer> cb, void* p) {
            id<MTLTexture> target = (__bridge id<MTLTexture>)p;
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit generateMipmapsForTexture:target];
            [blit endEncoding];
        }, (__bridge void*)tex);
        if (st.ok()) t->state = ResourceState::ShaderRead;
        return st;
    }
}

Status Backend::read_texture(TextureHandle src, void* outData, u32 bytesPerRow) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        Texture* t = d.textures.get(src.id);
        if (!t || !outData) return Errc::InvalidArgument;
        const u32 bpp = t->desc.bytes_per_pixel();
        if (bpp == 0) return Status{Errc::InvalidArgument, "formato sem bytes por texel"};
        const u32 w = t->desc.width;
        const u32 h = t->desc.height;
        const u32 rowBytes = w * bpp;
        const usize total = static_cast<usize>(rowBytes) * h;

        BufferDesc rd;
        rd.bytes = total;
        rd.usage = BufferUsage::TransferDst;
        rd.access = MemoryAccess::Readback;
        rd.debugName = "leitura-de-volta";
        auto readback = create_buffer(rd);
        if (!readback.ok()) return readback.status();
        Buffer* b = d.buffers.get(readback->id);
        if (!b) return Errc::InvalidState;
        // O ponteiro do buffer atravessa a submissão imediata: guardado antes,
        // porque `buffers_.get` pode ser invalidado por uma realocação do pool.
        id<MTLBuffer> target = b->buffer;
        const bool managed = b->managed;

        struct Ctx { id<MTLTexture> src; id<MTLBuffer> dst; u32 w, h, rowBytes; usize total; bool managed; }
            ctx{t->texture, target, w, h, rowBytes, total, managed};
        const Status st = d.submit_immediate([](Impl&, id<MTLCommandBuffer> cb, void* p) {
            auto* c = static_cast<Ctx*>(p);
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit copyFromTexture:c->src
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(c->w, c->h, 1)
                         toBuffer:c->dst
                destinationOffset:0
           destinationBytesPerRow:c->rowBytes
         destinationBytesPerImage:0];
#if TARGET_OS_OSX
            if (c->managed) [blit synchronizeResource:c->dst];
#endif
            [blit endEncoding];
        }, &ctx);
        if (st.ok()) {
            const u32 pitch = bytesPerRow ? bytesPerRow : rowBytes;
            const u8* mapped = static_cast<const u8*>(target.contents);
            if (mapped) {
                for (u32 y = 0; y < h; ++y) {
                    std::memcpy(static_cast<u8*>(outData) + static_cast<usize>(y) * pitch,
                                mapped + static_cast<usize>(y) * rowBytes, rowBytes);
                }
            }
        }
        Buffer dead;
        if (d.buffers.remove(readback->id, dead)) d.destroy_buffer_now(dead);
        return st;
    }
}

// =============================================================================
// Zero-copy: CVPixelBuffer do VideoToolbox → MTLTexture amostrável
//
// O `CVMetalTextureCache` embrulha o IOSurface do buffer sem copiar plano
// nenhum, e é o caminho que o decoder realmente usa: o VideoToolbox recicla um
// conjunto fixo de buffers, então a importação é feita uma vez por buffer e
// reaproveitada — em regime, importar custa zero.
//
// QUEM CONVERTE O YCbCr (a diferença honesta frente ao Vulkan, ver o comentário
// longo em `MetalBackend.hpp`): aqui a textura tem um `MTLPixelFormat` YCbCr
// biplanar, e a AMOSTRA já sai em R'G'B' pela matriz do formato — por isso o
// `ExternalTexture` sai com `rgb = true`. O shader do motor não muda: o ramo
// `sampling.w > 0.5` (`s.rgb`) é exatamente o certo para esta amostra, e aplicar
// a matriz ali seria convertê-la duas vezes.
// =============================================================================
Result<ExternalTexture> Backend::import_external_image(const ExternalImageDesc& img) noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (!d.device || !d.textureCache) {
            return Status{Errc::UnsupportedFeature, "importacao de imagem externa indisponivel"};
        }
        if (!img.nativeHandle) return Status{Errc::InvalidArgument, "buffer nulo"};
        CVPixelBufferRef pixelBuffer = static_cast<CVPixelBufferRef>(img.nativeHandle);

        const u64 colorKey = static_cast<u64>(img.matrix) | (img.fullRange ? 4ull : 0ull);
        if (auto it = d.importedByBuffer.find(pixelBuffer); it != d.importedByBuffer.end()) {
            Texture* cached = d.textures.get(it->second);
            if (cached && cached->colorKey != colorKey) {
                // A cor do vídeo mudou (formato de saída novo): o formato do
                // buffer carrega a matriz antiga e a importação precisa ser refeita.
                destroy_texture(TextureHandle{it->second});
                cached = nullptr;
            }
            if (Texture* t = cached) {
                t->lastUsedFrame = d.frameNumber;
                ExternalTexture out;
                out.texture = TextureHandle{it->second};
                // Sem sampler: o Metal não tem sampler imutável, e o formato da
                // textura já carrega a conversão. Isso colapsa o cache de
                // pipeline do motor num pipeline por formato de vídeo — o certo
                // aqui (não há estado de conversão a variar).
                out.sampler = SamplerHandle{};
                out.formatKey = cached->colorKey ^ (static_cast<u64>(cached->format) * 0x9E3779B97F4A7C15ull);
                out.rgb = t->externalRgb;
                return out;
            }
            d.importedByBuffer.erase(pixelBuffer);
        }

        const OSType cvFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
        MTLPixelFormat pixelFormat = MTLPixelFormatInvalid;
        switch (cvFormat) {
            case kCVPixelFormatType_32BGRA:
                pixelFormat = MTLPixelFormatBGRA8Unorm;
                break;
            case kCVPixelFormatType_32RGBA:
                pixelFormat = MTLPixelFormatRGBA8Unorm;
                break;
            default:
                // 3 planos (I420), 4:2:2, ARGB/ABGR: o motor lê UMA textura, e
                // esses formatos não cabem numa textura só. Falha honesta, com o
                // caminho apontado — em vez de amostrar o canal errado.
                AUREA_LOG_WARN("metal: CVPixelBuffer 0x%08x nao cabe numa textura (pedir 32BGRA ou usar planos YUV)",
                               static_cast<unsigned>(cvFormat));
                return Status{Errc::UnsupportedFormat, "formato de CVPixelBuffer nao suportado"};
        }
        const size_t width = CVPixelBufferGetWidth(pixelBuffer);
        const size_t height = CVPixelBufferGetHeight(pixelBuffer);
        if (width == 0 || height == 0) return Status{Errc::InvalidArgument, "buffer sem tamanho"};

        CVMetalTextureRef cvTexture = nullptr;
        CVReturn r = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, d.textureCache, pixelBuffer,
                                                              nil, pixelFormat, width, height, 0, &cvTexture);
        if (r != kCVReturnSuccess || !cvTexture) {
            AUREA_LOG_WARN("metal: CVMetalTextureCacheCreateTextureFromImage falhou (CVReturn %d)", static_cast<int>(r));
            return Status{Errc::UnsupportedFeature, "textura externa indisponivel"};
        }
        id<MTLTexture> texture = CVMetalTextureGetTexture(cvTexture);
        if (!texture) {
            CFRelease(cvTexture);
            return Status{Errc::UnsupportedFeature, "textura externa vazia"};
        }

        Texture t;
        t.texture = texture;
        t.format = pixelFormat;
        t.ownsTexture = false;          // a textura é do cache, não nossa
        t.external = true;
        // O buffer é BGRA/RGBA. O shader decodifica curva e primárias.
        t.externalRgb = true;
        t.colorKey = colorKey;
        t.cvTexture = cvTexture;
        // Retém o CVPixelBuffer: ele não pode voltar ao decoder enquanto a GPU
        // puder estar lendo a textura. A devolução é o `release_external_image`
        // (o motor chama no `defer_until_gpu_done`).
        CVPixelBufferRetain(pixelBuffer);
        t.pixelBuffer = pixelBuffer;
        t.desc.width = static_cast<u32>(width);
        t.desc.height = static_cast<u32>(height);
        t.desc.format = from_mtl(pixelFormat);
        t.desc.sampled = true;
        t.desc.renderTarget = false;
        t.desc.storage = false;
        t.desc.debugName = "frame-externo";
        t.lastUsedFrame = d.frameNumber;

        const u64 id = d.textures.add(std::move(t));
        d.importedByBuffer[pixelBuffer] = id;
        ++d.allocationCount;

        ExternalTexture out;
        out.texture = TextureHandle{id};
        out.sampler = SamplerHandle{};
        out.formatKey = colorKey ^ (static_cast<u64>(pixelFormat) * 0x9E3779B97F4A7C15ull);
        out.rgb = true;
        return out;
    }
}

void Backend::release_external_image(TextureHandle imported) noexcept { destroy_texture(imported); }

} // namespace aurea::mtl

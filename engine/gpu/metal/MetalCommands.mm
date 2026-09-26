// =============================================================================
//  Lista de comandos, render pass, medição e o ciclo do frame.
//
//  TRÊS COISAS QUE VALEM SABER ANTES DE LER:
//
//   1. BARREIRA. `barrier()` NÃO emite nada no Metal: só atualiza o estado
//      rastreado (que serve a diagnose e a futuras validações). Recursos criados
//      pelo `MTLDevice` têm hazard tracking, e o driver insere a dependência
//      sozinho entre encoders e entre passes. É a diferença semântica honesta
//      frente ao Vulkan, onde cada transição de layout é uma barreira explícita:
//      aqui não existe `VkImageLayout` e não existe transição a pagar. Se algum
//      dia um recurso passar a vir de `MTLHeap` com
//      `MTLHazardTrackingModeUntracked`, ESTE é o lugar que precisa de
//      `MTLBarrier` explícito.
//
//   2. UM ENCODER POR VEZ. Render, compute e blit não coexistem: criar um
//      encerra o outro. Por isso toda amarração vive num ESPELHO (`textures_`,
//      `uniformBuffer_`, `pushBytes_`…) que é reaplicado quando um encoder novo
//      começa ou quando o draw/dispatch acontece — o mesmo desenho do
//      `flush_descriptors` do Vulkan, sem conjunto de descritores.
//
//   3. O FENCE É A CONCLUSÃO DO COMMAND BUFFER. `begin_frame` espera o
//      contexto que vai reciclar (o `VkFence` por frame), e `wait_frame`
//      espera o frame pedido com tempo limite. O estado terminal também
//      preserva o erro real da GPU, que um evento pode nunca sinalizar.
// =============================================================================
#include "MetalInternal.hpp"

#include "aurea/core/Log.hpp"

#include <algorithm>
#include <cstring>

namespace aurea::mtl {

namespace {

MTLLoadAction to_mtl(LoadOp op) noexcept {
    switch (op) {
        case LoadOp::Clear:    return MTLLoadActionClear;
        case LoadOp::Load:     return MTLLoadActionLoad;
        case LoadOp::DontCare: return MTLLoadActionDontCare;
    }
    return MTLLoadActionLoad;
}

} // namespace

// =============================================================================
// Amarração do frame
// =============================================================================
void CommandListImpl::bind_frame(Backend* backend, FrameContext* frame) noexcept {
    end_current();
    backend_ = backend;
    impl_ = backend ? &backend->impl() : nullptr;
    frame_ = frame;
    cmd_ = frame ? frame->cmd : nil;
    pipeline_ = nullptr;
    pipelineDirty_ = true;
    for (TexBinding& t : textures_) t = TexBinding{};
    storageImages_[0] = storageImages_[1] = 0;
    storageBuffers_[0] = storageBuffers_[1] = 0;
    uniformBuffer_ = nil;
    uniformOffset_ = uniformSize_ = 0;
    pushSize_ = 0;
    pushDirty_ = true;
    for (VertexBinding& v : vertices_) v = VertexBinding{};
    indexBuffer_ = 0;
    indexOffset_ = 0;
    indexType_ = MTLIndexTypeUInt16;
    viewport_[0] = viewport_[1] = viewport_[2] = viewport_[3] = 0.0f;
    scissor_[0] = scissor_[1] = scissor_[2] = scissor_[3] = 0;
    texturesDirty_ = 0xFFFFu;   // todo slot começa no recurso de reserva
    storageDirty_ = true;
    dirty_ = kDirtyAll;
    labelDepth_ = 0;
}

// =============================================================================
// Encoders
// =============================================================================
void CommandListImpl::end_current() noexcept {
    bool ended = false;
    if (blit_) {
        [blit_ endEncoding];
        blit_ = nil;
        ended = true;
    }
    if (compute_) {
        [compute_ endEncoding];
        compute_ = nil;
        ended = true;
    }
    if (render_) {
        [render_ endEncoding];
        render_ = nil;
        ended = true;
    }
    enc_ = Enc::None;
    if (ended) {
        // Estado de encoder não sobrevive à troca: o espelho volta a valer.
        pipelineDirty_ = true;
        pushDirty_ = true;
        texturesDirty_ = 0xFFFFu;
        storageDirty_ = true;
        dirty_ = kDirtyAll;
    }
}

void CommandListImpl::finish_encoders() noexcept { end_current(); }

id<MTLBlitCommandEncoder> CommandListImpl::blit_encoder() noexcept {
    if (enc_ == Enc::Blit && blit_) return blit_;
    if (!cmd_) return nil;
    end_current();
    // The engine renders on a C++ thread without an ambient Cocoa pool.
    // Keep the strong encoder, but drain its autoreleased factory reference.
    @autoreleasepool { blit_ = [cmd_ blitCommandEncoder]; }
    if (!blit_) return nil;
    enc_ = Enc::Blit;
    return blit_;
}

id<MTLComputeCommandEncoder> CommandListImpl::compute_encoder() noexcept {
    if (enc_ == Enc::Compute && compute_) return compute_;
    if (!cmd_) return nil;
    end_current();
    @autoreleasepool { compute_ = [cmd_ computeCommandEncoder]; }
    if (!compute_) return nil;
    enc_ = Enc::Compute;
    return compute_;
}

// =============================================================================
// Barreira: só estado (ver o comentário do topo do arquivo)
// =============================================================================
void CommandListImpl::barrier(TextureHandle texture, ResourceState newState, bool discard) noexcept {
    (void)discard;
    if (!impl_) return;
    Texture* t = impl_->textures.get(texture.id);
    if (!t) return;
    if (enc_ == Enc::Render) {
        // Dentro de um render pass não há ordem de comando a impor: o Metal
        // ordena por dependência. Registrar é o que se pode fazer de honesto.
        AUREA_LOG_WARN("metal: barreira dentro de render pass (ignorada; o Metal ordena por dependencia)");
        return;
    }
    t->state = newState;
}

// =============================================================================
// Render pass
// =============================================================================
void CommandListImpl::begin_render_pass(const RenderPassBegin& pass) noexcept {
    if (!cmd_ || !impl_) return;
    // Um compute pode ter acabado de escrever a imagem que este passe vai ler:
    // em Metal o encoder dele tem de ser encerrado antes do de render.
    if (enc_ != Enc::None) end_current();
    Impl& d = *impl_;
    Texture* color = pass.color.valid() ? d.textures.get(pass.color.id) : nullptr;
    Texture* depth = pass.depth.valid() ? d.textures.get(pass.depth.id) : nullptr;
    if (pass.color.valid() && !color) return;
    if (pass.depth.valid() && !depth) return;
    if (!color && !depth) return;

    @autoreleasepool {
        const u32 width = color ? color->desc.width : depth->desc.width;
        const u32 height = color ? color->desc.height : depth->desc.height;
        MTLRenderPassDescriptor* rp = [[MTLRenderPassDescriptor alloc] init];
        if (color) {
            rp.colorAttachments[0].texture = color->texture;
            rp.colorAttachments[0].loadAction = to_mtl(pass.load);
            // O alvo da cor sempre é guardado: é o resultado do passe.
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            rp.colorAttachments[0].clearColor = MTLClearColorMake(pass.clear[0], pass.clear[1],
                                                                 pass.clear[2], pass.clear[3]);
        }
        if (depth) {
            rp.depthAttachment.texture = depth->texture;
            rp.depthAttachment.loadAction = to_mtl(pass.depthLoad);
            rp.depthAttachment.clearDepth = pass.clearDepth;
            // Profundidade só de teste não precisa ir para a memória — em GPU
            // tile-based isso economiza a escrita inteira do anexo.
            rp.depthAttachment.storeAction = pass.storeDepth ? MTLStoreActionStore : MTLStoreActionDontCare;
        }
        render_ = [cmd_ renderCommandEncoderWithDescriptor:rp];
        if (!render_) {
            AUREA_LOG_ERROR("metal: render pass nao abriu (formato/anexo incompativeis)");
            return;
        }
        enc_ = Enc::Render;
        // Viewport e scissor padrão = alvo inteiro. Sem chamada do motor, é isto
        // que vale — igual ao Vulkan, onde o grafo também herda o alvo inteiro.
        viewport_[0] = 0.0f;
        viewport_[1] = 0.0f;
        viewport_[2] = static_cast<f32>(width);
        viewport_[3] = static_cast<f32>(height);
        scissor_[0] = 0;
        scissor_[1] = 0;
        scissor_[2] = static_cast<i32>(width);
        scissor_[3] = static_cast<i32>(height);
        [render_ setViewport:MTLViewport{0.0, 0.0, static_cast<double>(width), static_cast<double>(height), 0.0, 1.0}];
        [render_ setScissorRect:MTLScissorRect{0, 0, static_cast<NSUInteger>(width),
                                               static_cast<NSUInteger>(height)}];
        if (frame_) {
            const u64 used = frame_->frameNumber;
            if (color) color->lastUsedFrame = used;
            if (depth) depth->lastUsedFrame = used;
        }
        pipelineDirty_ = true;
        pushDirty_ = true;
        texturesDirty_ = 0xFFFFu;
        storageDirty_ = true;
        dirty_ = kDirtyAll;
    }
    if (color) color->state = ResourceState::ColorAttachment;
    if (depth) depth->state = ResourceState::DepthAttachment;
}

void CommandListImpl::end_render_pass() noexcept {
    if (enc_ != Enc::Render) return;
    end_current();
}

// =============================================================================
// Pipeline e amarrações
// =============================================================================
void CommandListImpl::bind_pipeline(PipelineHandle pipeline) noexcept {
    if (!impl_) return;
    const PipelineObject* p = impl_->pipeline(pipeline.id);
    if (!p) return;
    if (p == pipeline_) return;
    pipeline_ = p;
    pipelineDirty_ = true;
    if (p->isCompute) {
        // O encoder de compute nasce aqui: a partir daqui todo bind_* já tem
        // onde acontecer (em Metal não existe amarrar sem encoder).
        (void)compute_encoder();
    }
}

void CommandListImpl::bind_texture(u32 slotIndex, TextureHandle texture, SamplerHandle sampler) noexcept {
    if (slotIndex >= binding::kTextureSlots) return;
    textures_[slotIndex] = TexBinding{texture.id, sampler.id};
    texturesDirty_ |= static_cast<u16>(1u << slotIndex);
}

void CommandListImpl::bind_storage_image(u32 slotIndex, TextureHandle texture) noexcept {
    if (slotIndex >= binding::kStorageImageSlots) return;
    storageImages_[slotIndex] = texture.id;
    storageDirty_ = true;
}

void CommandListImpl::bind_storage_buffer(BufferHandle buffer) noexcept {
    storageBuffers_[0] = buffer.id;
    storageDirty_ = true;
}

void CommandListImpl::bind_storage_buffer_at(u32 slotIndex, BufferHandle buffer) noexcept {
    if (slotIndex >= binding::kStorageBufferSlots) return;
    storageBuffers_[slotIndex] = buffer.id;
    storageDirty_ = true;
}

void CommandListImpl::set_uniforms(const void* data, u32 bytes) noexcept {
    if (!impl_ || !frame_ || !data || bytes == 0) return;
    id<MTLBuffer> buffer = nil;
    u32 offset = 0;
    void* ptr = nullptr;
    if (!frame_->uniforms.allocate(bytes, slot::kRingAlignment, buffer, offset, ptr)) return;
    std::memcpy(ptr, data, bytes);
    if (buffer != uniformBuffer_ || offset != uniformOffset_ || bytes != uniformSize_) {
        dirty_ |= kDirtyUniform;
    }
    uniformBuffer_ = buffer;
    uniformOffset_ = offset;
    uniformSize_ = bytes;
}

void CommandListImpl::push_constants(const void* data, u32 bytes) noexcept {
    if (!data || bytes == 0 || !pipeline_) return;
    const u32 n = std::min<u32>(bytes, binding::kPushConstantBytes);
    std::memcpy(pushBytes_, data, n);
    pushSize_ = n;
    pushDirty_ = true;
}

void CommandListImpl::set_viewport(f32 x, f32 y, f32 w, f32 h) noexcept {
    viewport_[0] = x;
    viewport_[1] = y;
    viewport_[2] = w;
    viewport_[3] = h;
    dirty_ |= kDirtyViewport;
}

void CommandListImpl::set_scissor(i32 x, i32 y, u32 w, u32 h) noexcept {
    scissor_[0] = x;
    scissor_[1] = y;
    scissor_[2] = static_cast<i32>(w);
    scissor_[3] = static_cast<i32>(h);
    dirty_ |= kDirtyScissor;
}

id<MTLTexture> CommandListImpl::resolve_texture(u64 id) const noexcept {
    if (!impl_) return nil;
    if (const Texture* t = impl_->textures.get(id)) return t->texture;
    const Texture* dummy = impl_->textures.get(impl_->dummyTexture);
    return dummy ? dummy->texture : nil;
}

id<MTLSamplerState> CommandListImpl::resolve_sampler(u64 id) const noexcept {
    if (impl_) {
        if (const SamplerObject* s = impl_->samplers.get(id)) return s->sampler;
    }
    return impl_ ? impl_->defaultSampler : nil;
}

void CommandListImpl::apply_pipeline_state() noexcept {
    if (!pipeline_) return;
    if (pipeline_->isCompute) {
        if (compute_) [compute_ setComputePipelineState:pipeline_->compute];
    } else if (render_) {
        [render_ setRenderPipelineState:pipeline_->render];
        if (pipeline_->depth) [render_ setDepthStencilState:pipeline_->depth];
        // Cull, winding e viés de profundidade moram no ENCODER no Metal (não no
        // pipeline), então são reaplicados a cada encoder novo.
        [render_ setCullMode:pipeline_->cull];
        [render_ setFrontFacingWinding:pipeline_->winding];
        // O viés de profundidade do mapa de sombra mora no ENCODER.
        const f32 bias = pipeline_->depthBiasEnabled ? pipeline_->depthBias : 0.0f;
        const f32 slope = pipeline_->depthBiasEnabled ? pipeline_->depthBiasSlope : 0.0f;
        [render_ setDepthBias:bias slopeScale:slope clamp:0.0f];
    }
    if (pushDirty_ && pushSize_ > 0) {
        if (render_) {
            [render_ setVertexBytes:pushBytes_ length:pushSize_ atIndex:slot::kPushConstant];
            [render_ setFragmentBytes:pushBytes_ length:pushSize_ atIndex:slot::kPushConstant];
        } else if (compute_) {
            [compute_ setBytes:pushBytes_ length:pushSize_ atIndex:slot::kPushConstant];
        }
        pushDirty_ = false;
    }
    pipelineDirty_ = false;
}

void CommandListImpl::apply_bindings() noexcept {
    if (!impl_) return;
    Impl& d = *impl_;
    const bool compute = (enc_ == Enc::Compute);
    if (!compute && !render_) return;

    if (texturesDirty_) {
        for (u32 i = 0; i < binding::kTextureSlots; ++i) {
            if (!(texturesDirty_ & static_cast<u16>(1u << i))) continue;
            id<MTLTexture> tex = resolve_texture(textures_[i].texture);
            id<MTLSamplerState> samp = resolve_sampler(textures_[i].sampler);
            if (!tex || !samp) continue;
            if (compute) {
                [compute_ setTexture:tex atIndex:i];
                [compute_ setSamplerState:samp atIndex:i];
            } else {
                // O layout universal do motor declara as texturas para TODOS os
                // estágios (é o `stageFlags = all` do Vulkan): amarrar nos dois
                // estágios é o que mantém um shader de vértice que amostra
                // funcionando sem o motor precisar saber de estágios.
                [render_ setVertexTexture:tex atIndex:i];
                [render_ setFragmentTexture:tex atIndex:i];
                [render_ setVertexSamplerState:samp atIndex:i];
                [render_ setFragmentSamplerState:samp atIndex:i];
            }
        }
        texturesDirty_ = 0;
    }

    if (dirty_ & kDirtyUniform) {
        id<MTLBuffer> buffer = uniformBuffer_;
        u32 offset = uniformOffset_;
        if (!buffer) {
            const Buffer* dummy = d.buffers.get(d.dummyBuffer);
            buffer = dummy ? dummy->buffer : nil;
            offset = 0;
        }
        if (buffer) {
            if (compute) {
                [compute_ setBuffer:buffer offset:offset atIndex:binding::kUniform];
            } else {
                [render_ setVertexBuffer:buffer offset:offset atIndex:binding::kUniform];
                [render_ setFragmentBuffer:buffer offset:offset atIndex:binding::kUniform];
            }
        }
        dirty_ &= ~kDirtyUniform;
    }

    if (storageDirty_) {
        const Buffer* dummy = d.buffers.get(d.dummyBuffer);
        id<MTLBuffer> fallback = dummy ? dummy->buffer : nil;
        for (u32 i = 0; i < binding::kStorageBufferSlots; ++i) {
            const u32 index = binding::kStorageBuffer + i;
            const Buffer* b = storageBuffers_[i] ? d.buffers.get(storageBuffers_[i]) : nullptr;
            id<MTLBuffer> buffer = b ? b->buffer : fallback;
            if (!buffer) continue;
            if (compute) {
                [compute_ setBuffer:buffer offset:0 atIndex:index];
            } else {
                [render_ setVertexBuffer:buffer offset:0 atIndex:index];
                [render_ setFragmentBuffer:buffer offset:0 atIndex:index];
            }
        }
        // Imagens de armazenamento: só fragmento e compute (é o `stageFlags` do
        // Vulkan para elas) — e o slot não usado lê a textura de reserva.
        const Texture* dummyStorage = d.textures.get(d.dummyStorage);
        for (u32 i = 0; i < binding::kStorageImageSlots; ++i) {
            const u32 index = binding::kStorageImage0 + i;
            const Texture* t = storageImages_[i] ? d.textures.get(storageImages_[i]) : nullptr;
            id<MTLTexture> tex = t ? t->texture : (dummyStorage ? dummyStorage->texture : nil);
            if (!tex) continue;
            if (compute) {
                [compute_ setTexture:tex atIndex:index];
            } else {
                [render_ setFragmentTexture:tex atIndex:index];
            }
        }
        storageDirty_ = false;
    }

    if (render_ && (dirty_ & kDirtyVertex)) {
        const Buffer* dummy = d.buffers.get(d.dummyBuffer);
        for (u32 b = 0; b < VertexLayout::kMaxBindings; ++b) {
            const Buffer* buffer = vertices_[b].buffer ? d.buffers.get(vertices_[b].buffer) : nullptr;
            id<MTLBuffer> handle = buffer ? buffer->buffer : (dummy ? dummy->buffer : nil);
            if (!handle) continue;
            [render_ setVertexBuffer:handle offset:vertices_[b].offset atIndex:b];
        }
        dirty_ &= ~kDirtyVertex;
    }

    if (render_ && (dirty_ & kDirtyIndex)) {
        // Metal recebe o buffer de índices diretamente em drawIndexedPrimitives.
        dirty_ &= ~kDirtyIndex;
    }

    if (render_ && (dirty_ & kDirtyViewport)) {
        [render_ setViewport:MTLViewport{static_cast<double>(viewport_[0]), static_cast<double>(viewport_[1]),
                                        static_cast<double>(viewport_[2]), static_cast<double>(viewport_[3]),
                                        0.0, 1.0}];
        dirty_ &= ~kDirtyViewport;
    }
    if (render_ && (dirty_ & kDirtyScissor)) {
        [render_ setScissorRect:MTLScissorRect{static_cast<NSUInteger>(std::max(0, scissor_[0])),
                                               static_cast<NSUInteger>(std::max(0, scissor_[1])),
                                               static_cast<NSUInteger>(std::max(0, scissor_[2])),
                                               static_cast<NSUInteger>(std::max(0, scissor_[3]))}];
        dirty_ &= ~kDirtyScissor;
    }
}

bool CommandListImpl::prepare_draw() noexcept {
    if (!render_ || !pipeline_ || pipeline_->isCompute) return false;
    if (pipelineDirty_ || pushDirty_) apply_pipeline_state();
    apply_bindings();
    return true;
}

// =============================================================================
// Draw, geometria e dispatch
// =============================================================================
void CommandListImpl::draw(u32 vertexCount, u32 instanceCount, u32 firstVertex) noexcept {
    if (!prepare_draw()) return;
    if (instanceCount <= 1) {
        [render_ drawPrimitives:pipeline_->topology vertexStart:firstVertex vertexCount:vertexCount];
    } else {
        [render_ drawPrimitives:pipeline_->topology
                    vertexStart:firstVertex
                    vertexCount:vertexCount
                  instanceCount:instanceCount];
    }
}

void CommandListImpl::bind_vertex_buffer(u32 bindingIndex, BufferHandle buffer, u64 offset) noexcept {
    if (bindingIndex >= VertexLayout::kMaxBindings) return;
    vertices_[bindingIndex] = VertexBinding{buffer.id, offset};
    dirty_ |= kDirtyVertex;
}

void CommandListImpl::bind_index_buffer(BufferHandle buffer, u64 offset, IndexType type) noexcept {
    indexBuffer_ = buffer.id;
    indexOffset_ = offset;
    indexType_ = to_mtl(type);
    dirty_ |= kDirtyIndex;
}

void CommandListImpl::draw_indexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset,
                                   u32 firstInstance) noexcept {
    if (!prepare_draw()) return;
    const Buffer* buffer = indexBuffer_ ? impl_->buffers.get(indexBuffer_) : nullptr;
    if (!buffer) return;
    // `firstIndex` entra como deslocamento no buffer de índices (o Vulkan faz a
    // mesma conta por baixo dos panos).
    const u64 indexSize = indexType_ == MTLIndexTypeUInt16 ? 2u : 4u;
    [render_ drawIndexedPrimitives:pipeline_->topology
                        indexCount:indexCount
                         indexType:indexType_
                       indexBuffer:buffer->buffer
                 indexBufferOffset:indexOffset_ + static_cast<u64>(firstIndex) * indexSize
                     instanceCount:instanceCount
                        baseVertex:vertexOffset
                      baseInstance:firstInstance];
}

void CommandListImpl::dispatch(u32 x, u32 y, u32 z) noexcept {
    if (!impl_ || !pipeline_ || !pipeline_->isCompute) return;
    if (!compute_encoder()) return;
    if (pipelineDirty_ || pushDirty_) apply_pipeline_state();
    apply_bindings();
    if (!compute_ || !pipeline_->compute) return;
    const u32 tx = std::max(1u, pipeline_->threadgroup[0]);
    const u32 ty = std::max(1u, pipeline_->threadgroup[1]);
    const u32 tz = std::max(1u, pipeline_->threadgroup[2]);
    [compute_ dispatchThreadgroups:MTLSizeMake(x, y, z)
            threadsPerThreadgroup:MTLSizeMake(tx, ty, tz)];
}

// =============================================================================
// Cópias
// =============================================================================
void CommandListImpl::copy_texture(TextureHandle src, TextureHandle dst) noexcept {
    if (!impl_ || enc_ == Enc::Render) return;
    const Texture* s = impl_->textures.get(src.id);
    const Texture* d = impl_->textures.get(dst.id);
    if (!s || !d || !s->texture || !d->texture) return;
    id<MTLBlitCommandEncoder> blit = blit_encoder();
    if (!blit) return;
    [blit copyFromTexture:s->texture
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(std::min(s->desc.width, d->desc.width),
                                      std::min(s->desc.height, d->desc.height), 1)
                toTexture:d->texture
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
}

void CommandListImpl::copy_texture_to_buffer(TextureHandle src, BufferHandle dst) noexcept {
    if (!impl_ || enc_ == Enc::Render) return;
    const Texture* s = impl_->textures.get(src.id);
    Buffer* b = impl_->buffers.get(dst.id);
    if (!s || !b || !s->texture || !b->buffer) return;
    id<MTLBlitCommandEncoder> blit = blit_encoder();
    if (!blit) return;
    const u32 rowBytes = s->desc.width * s->desc.bytes_per_pixel();
    [blit copyFromTexture:s->texture
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(s->desc.width, s->desc.height, 1)
                 toBuffer:b->buffer
        destinationOffset:0
   destinationBytesPerRow:rowBytes
 destinationBytesPerImage:0];
    // A CPU lê o buffer depois do fence (export): no Mac Intel a escrita da GPU
    // precisa ser sincronizada para o domínio do host.
#if TARGET_OS_OSX
    if (b->managed) [blit synchronizeResource:b->buffer];
#endif
}

// =============================================================================
// Medição e rótulos
// =============================================================================
void CommandListImpl::begin_timer(const char* label) noexcept {
    if (!impl_ || !frame_ || !impl_->timersEnabled || enc_ == Enc::Render) return;
    const u32 index = static_cast<u32>(frame_->timerLabels.size());
    // O teto é o do buffer que o device aceitou, não o do desejo do motor.
    if (index >= impl_->maxTimerSlots) return;
    frame_->timerLabels.push_back(label);
    frame_->timerStack.push_back(index);
    impl_->sample_counter(2 + index * 2);
    frame_->timersWritten = true;
}

void CommandListImpl::end_timer() noexcept {
    if (!impl_ || !frame_ || frame_->timerStack.empty() || enc_ == Enc::Render) return;
    const u32 index = frame_->timerStack.back();
    frame_->timerStack.pop_back();
    impl_->sample_counter(2 + index * 2 + 1);
}

void CommandListImpl::begin_label(const char* label) noexcept {
    if (!cmd_ || !label) return;
    @autoreleasepool {
        [cmd_ pushDebugGroup:[NSString stringWithUTF8String:label]];
    }
    ++labelDepth_;
}

void CommandListImpl::end_label() noexcept {
    if (!cmd_ || labelDepth_ == 0) return;
    [cmd_ popDebugGroup];
    --labelDepth_;
}

// =============================================================================
// Amostra de contador e devolução de importações ociosas
// =============================================================================
void Impl::sample_counter(u32 index) noexcept {
    if (!timersEnabled || !current || !current->counterBuffer || !current->cmd) return;
    if (index >= current->counterSamples) return;
    if (@available(iOS 14.0, macOS 10.15, *)) {
        // Um encoder de blit SÓ para a amostra, com barreira: é o que mede o
        // intervalo entre passes sem tocar no estado de nenhum passe (e o único
        // jeito de amostrar fora de um encoder de render).
        commands.finish_encoders();
        @autoreleasepool {
            id<MTLBlitCommandEncoder> blit = [current->cmd blitCommandEncoder];
            if (!blit) return;
            [blit sampleCountersInBuffer:current->counterBuffer atSampleIndex:index withBarrier:YES];
            [blit endEncoding];
        }
    }
}

void Impl::reclaim_stale_imports() noexcept {
    if (importedByBuffer.empty()) return;
    std::vector<u64> stale;
    for (const auto& [buffer, id] : importedByBuffer) {
        const Texture* t = textures.get(id);
        // Retaining a CVPixelBuffer prevents AVFoundation from recycling it.
        // Hundreds of retained 4K buffers can exhaust an iPhone in seconds.
        // The deferred destroy queue still protects every in-flight GPU read.
        if (!t || frameNumber > t->lastUsedFrame + framesInFlight) stale.push_back(id);
    }
    for (u64 id : stale) self->destroy_texture(TextureHandle{id});
}

// =============================================================================
// Frame
// =============================================================================
Status Backend::begin_frame(FrameBegin& out) noexcept {
    return impl_->begin_frame_impl(out, true);
}

Status Backend::begin_offscreen_frame(FrameBegin& out) noexcept {
    return impl_->begin_frame_impl(out, false);
}

Status Impl::begin_frame_impl(FrameBegin& out, bool withSurface) noexcept {
    out = FrameBegin{};
    if (!initialized) return Status{Errc::InvalidState, "backend nao inicializado"};
    if (deviceLost) return Status{Errc::DeviceLost, "dispositivo perdido"};
    if (current) return Status{Errc::InvalidState, "frame ja aberto"};

    FrameContext& f = frames[frameCursor];
    if (f.submitted) {
        // O fence do frame que vai ser reciclado. 2 s: passou disso, a GPU está
        // travada e o frame é pulado em vez de travar o app para sempre.
        const Status status = wait_command_buffer(f.cmd, f.completion, 2'000'000'000ull, "reciclar frame");
        if (!status.ok()) return status;
        f.submitted = false;
        collect_timings(f);
        run_deferred(f);
        reclaim_stale_imports();
    }
    f.frameNumber = ++frameNumber;
    f.uniforms.reset();
    f.staging.reset();
    f.timerLabels.clear();
    f.timerStack.clear();
    uploadBytesFrame = 0;

    @autoreleasepool {
        // O command buffer do frame anterior é liberado aqui — depois do fence.
        f.cmd = [queue commandBuffer];
    }
    if (!f.cmd) return Status{Errc::InvalidState, "sem command buffer"};
    f.cmd.label = @"frame";
    f.completion = command_buffer_completion(f.cmd);
    if (!f.completion) return Status{Errc::OutOfMemory, "sem fence de command buffer"};

    current = &f;
    commands.bind_frame(self, &f);
    out.commands = &commands;
    out.frameNumber = f.frameNumber;

    // Marca de tempo do começo do frame (índice 0) — o fim (1) vai no end_frame.
    sample_counter(0);

    // Frame offscreen (prévia de efeito, export): não adquire nem apresenta
    // nada. Apresentar daqui seria apresentar o quadro errado.
    drawable = nil;
    drawableAcquired = false;
    if (!withSurface) return OkStatus;
    if (!layer) return OkStatus;

    @autoreleasepool {
        drawable = [layer nextDrawable];
    }
    if (!drawable) {
        // Sem drawable neste instante (o sistema está ocupado): o frame segue sem
        // backbuffer — gravado e submetido, o que mantém os fences andando.
        AUREA_LOG_WARN("metal: nextDrawable sem imagem; frame sem backbuffer");
        return OkStatus;
    }
    const u32 width = static_cast<u32>(drawable.texture.width);
    const u32 height = static_cast<u32>(drawable.texture.height);
    out.backbuffer = register_drawable(drawable.texture, width, height);
    out.backbufferWidth = width;
    out.backbufferHeight = height;
    out.backbufferFormat = from_mtl(layer.pixelFormat);
    // Nada de pré-rotação: no iOS quem gira é o sistema (ver MetalBackend.hpp).
    out.rotation = SurfaceRotation::None;
    drawableAcquired = true;
    return OkStatus;
}

Status Backend::end_frame() noexcept {
    @autoreleasepool {
        Impl& d = *impl_;
        if (!d.current) return Status{Errc::InvalidState, "nenhum frame aberto"};
        FrameContext& f = *d.current;

        d.commands.end_render_pass();
        d.sample_counter(1);
        d.commands.finish_encoders();   // nenhum encoder pode ficar aberto no commit

        if (d.drawableAcquired && d.drawable) [f.cmd presentDrawable:d.drawable];
        [f.cmd commit];

        d.current = nullptr;
        d.commands.bind_frame(this, nullptr);
        d.frameCursor = (d.frameCursor + 1) % d.framesInFlight;
        f.submitted = true;
        d.lastSubmitted = &f;
        d.drawableAcquired = false;
        // As texturas externas lidas neste frame não precisam de barreira de
        // devolução: em Metal não há posse de fila a transferir (o Vulkan faz
        // isso porque lá o AHardwareBuffer é compartilhado entre donos).
        return OkStatus;
    }
}

} // namespace aurea::mtl

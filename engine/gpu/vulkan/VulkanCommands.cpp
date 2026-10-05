// =============================================================================
//  Frame, lista de comandos, barreiras, descritores, medição e destruição adiada.
// =============================================================================
#include "VulkanBackend.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>
#include <cstring>

namespace aurea::vk {
namespace {

struct StateInfo {
    VkImageLayout layout;
    VkPipelineStageFlags stage;
    VkAccessFlags access;
    bool write;
};

StateInfo state_info(ResourceState s) noexcept {
    switch (s) {
        case ResourceState::Undefined:
            return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, false};
        case ResourceState::ShaderRead:
            return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT, false};
        case ResourceState::ColorAttachment:
            return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, true};
        case ResourceState::StorageWrite:
            return {VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT, true};
        case ResourceState::StorageReadWrite:
            return {VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, true};
        case ResourceState::TransferSrc:
            return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT, false};
        case ResourceState::TransferDst:
            return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT, true};
        case ResourceState::Present:
            return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, false};
        case ResourceState::DepthAttachment:
            return {VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, true};
    }
    return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, false};
}

u32 foreign_family(bool hasForeign) noexcept {
    return hasForeign ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_EXTERNAL;
}

} // namespace

// =============================================================================
// Barreira mínima a partir do estado rastreado
// =============================================================================
void Backend::transition(VkCommandBuffer cmd, Texture& t, ResourceState newState, bool discard) noexcept {
    if (t.state == newState && !discard) {
        // Mesmo estado de só-leitura: nada a fazer. Mesmo estado de escrita
        // (storage → storage): ainda precisa ordenar escrita-depois-de-escrita.
        if (!state_info(newState).write || newState == ResourceState::ColorAttachment
            || newState == ResourceState::DepthAttachment) return;
    }
    const StateInfo from = state_info(t.state);
    const StateInfo to = state_info(newState);

    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = from.access & (VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                     | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    b.dstAccessMask = to.access;
    b.oldLayout = discard ? VK_IMAGE_LAYOUT_UNDEFINED : t.layout;
    b.newLayout = to.layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t.image;
    b.subresourceRange = {aspect_of(t.format), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    VkPipelineStageFlags srcStage = from.stage;

    if (imageAcquired_ && t.state == ResourceState::Undefined
        && imageIndex_ < swapTextures_.size() && textures_.get(swapTextures_[imageIndex_]) == &t) {
        // Discarding the old pixels does not end the presentation engine's
        // read. Chain the first layout transition to the acquire semaphore's
        // COLOR_ATTACHMENT_OUTPUT wait in end_frame; TOP_OF_PIPE would allow
        // that transition to race the preceding presentation under load.
        // Other images keep their own source stages, so offscreen work can
        // still overlap acquisition of the window image.
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }

    if (t.external && !t.acquiredThisFrame) {
        // Frame do decoder: a imagem pertence a uma "fila estrangeira" (o
        // MediaCodec). Aquisição de posse com layout UNDEFINED, que para AHB
        // preserva o conteúdo — é o que o Skia e o Chromium fazem.
        b.srcQueueFamilyIndex = foreign_family(hasForeignQueue_);
        b.dstQueueFamilyIndex = graphicsFamily_;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.srcAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        t.acquiredThisFrame = true;
        if (current_) {
            u64 id = 0;
            for (auto& [buf, tid] : importedByBuffer_) {
                if (buf == t.nativeBuffer) { id = tid; break; }
            }
            if (id) current_->externalAcquired.push_back(id);
        }
    }

    vkCmdPipelineBarrier(cmd, srcStage, to.stage, 0, 0, nullptr, 0, nullptr, 1, &b);
    t.layout = to.layout;
    t.state = newState;
}

// =============================================================================
// Descritores: pools por frame, resetados quando o fence do frame volta.
// =============================================================================
VkDescriptorSet Backend::allocate_set(FrameContext& frame, VkDescriptorSetLayout layout) noexcept {
    for (u32 attempt = 0; attempt < 2; ++attempt) {
        if (frame.descriptorPoolCursor < frame.descriptorPools.size()) {
            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = frame.descriptorPools[frame.descriptorPoolCursor];
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &layout;
            VkDescriptorSet set = VK_NULL_HANDLE;
            const VkResult r = vkAllocateDescriptorSets(device_, &ai, &set);
            if (r == VK_SUCCESS) return set;
            ++frame.descriptorPoolCursor;   // cheio: próximo pool
            continue;
        }
        // Nenhum pool com espaço: cria mais um para este frame. Em regime o
        // número de pools estabiliza e nada mais é criado.
        VkDescriptorPoolSize sizes[4] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512 * binding::kTextureSlots},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 512},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512 * binding::kStorageImageSlots},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 512 * binding::kStorageBufferSlots},
        };
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 512;
        pi.poolSizeCount = 4;
        pi.pPoolSizes = sizes;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (vkCreateDescriptorPool(device_, &pi, nullptr, &pool) != VK_SUCCESS) return VK_NULL_HANDLE;
        frame.descriptorPools.push_back(pool);
        frame.descriptorPoolCursor = static_cast<u32>(frame.descriptorPools.size() - 1);
        attempt = 0;
    }
    return VK_NULL_HANDLE;
}

// =============================================================================
// CommandListImpl
// =============================================================================
void CommandListImpl::bind_frame(Backend* backend, FrameContext* frame, VkCommandBuffer cmd) noexcept {
    backend_ = backend;
    frame_ = frame;
    cmd_ = cmd;
    inRenderPass_ = false;
    pipeline_ = nullptr;
    for (TexBinding& t : textures_) t = TexBinding{};
    for (u64& s : storageImages_) s = 0;
    storageBuffer_ = 0;
    storageBuffer1_ = 0;
    uniformBuffer_ = VK_NULL_HANDLE;
    uniformOffset_ = uniformSize_ = 0;
    lastSet_ = VK_NULL_HANDLE;
    dirty_ = true;
    vertices_.clear();
    indexBuffer_ = VK_NULL_HANDLE;
    indexOffset_ = 0;
    hasViewport_ = hasScissor_ = false;
    pushLayout_ = VK_NULL_HANDLE;
    pushBytes_ = labelDepth_ = 0;
}

void CommandListImpl::barrier(TextureHandle texture, ResourceState newState, bool discard) noexcept {
    if (!cmd_) return;
    Texture* t = backend_->texture(texture.id);
    if (!t) return;
    if (inRenderPass_) {
        AUREA_LOG_ERROR("barreira dentro de render pass: ignorada (erro de uso do grafo)");
        return;
    }
    backend_->transition(cmd_, *t, newState, discard);
}

void CommandListImpl::begin_render_pass(const RenderPassBegin& pass) noexcept {
    if (!cmd_) return;
    if (inRenderPass_) return;
    if (pass.depth.valid() && (pass.color1.valid() || pass.resolve.valid()
                               || (pass.color.valid() && backend_->texture(pass.color.id)
                                   && backend_->texture(pass.color.id)->desc.sampleCount > 1))) {
        // 3D com MSAA / MRT / resolve.
        Texture* c0 = pass.color.valid() ? backend_->texture(pass.color.id) : nullptr;
        Texture* c1 = pass.color1.valid() ? backend_->texture(pass.color1.id) : nullptr;
        Texture* d = backend_->texture(pass.depth.id);
        Texture* r0 = pass.resolve.valid() ? backend_->texture(pass.resolve.id) : nullptr;
        Texture* r1 = pass.resolve1.valid() ? backend_->texture(pass.resolve1.id) : nullptr;
        const bool wantDepthResolve = pass.depthResolve.valid() && backend_->depth_resolve_supported() && r0;
        Texture* dr = wantDepthResolve ? backend_->texture(pass.depthResolve.id) : nullptr;
        if (!d || !c0 || (pass.color1.valid() && !c1) || (pass.resolve.valid() && !r0)
            || (pass.resolve1.valid() && !r1)) return;
        const bool discardColor = pass.load != LoadOp::Load;
        for (Texture* t : {c0, c1}) {
            if (t && t->state != ResourceState::ColorAttachment) backend_->transition(cmd_, *t, ResourceState::ColorAttachment, discardColor);
        }
        if (d->state != ResourceState::DepthAttachment) {
            backend_->transition(cmd_, *d, ResourceState::DepthAttachment, pass.depthLoad != LoadOp::Load);
        }
        for (Texture* t : {r0, r1}) {
            if (t && t->state != ResourceState::ColorAttachment) backend_->transition(cmd_, *t, ResourceState::ColorAttachment, true);
        }
        if (dr && dr->state != ResourceState::DepthAttachment) backend_->transition(cmd_, *dr, ResourceState::DepthAttachment, true);

        Backend::RenderPassKey key;
        key.color0 = c0->format;
        key.color1 = c1 ? c1->format : VK_FORMAT_UNDEFINED;
        key.depth = d->format;
        key.samples = std::max(1u, c0->desc.sampleCount);
        key.load = pass.load;
        key.depthLoad = pass.depthLoad;
        key.storeDepth = pass.storeDepth;
        key.resolve = r0 != nullptr;
        key.depthResolve = dr != nullptr;
        u64 ids[6] = {pass.color.id, c1 ? pass.color1.id : 0, pass.depth.id, r0 ? pass.resolve.id : 0,
                      r1 ? pass.resolve1.id : 0, dr ? pass.depthResolve.id : 0};
        VkImageView views[6] = {c0->view, c1 ? c1->view : VK_NULL_HANDLE, d->view, r0 ? r0->view : VK_NULL_HANDLE,
                                r1 ? r1->view : VK_NULL_HANDLE, dr ? dr->view : VK_NULL_HANDLE};
        VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        info.renderPass = backend_->render_pass(key);
        info.framebuffer = backend_->framebuffer(key, ids, views, d->desc.width, d->desc.height);
        info.renderArea = {{0, 0}, {d->desc.width, d->desc.height}};
        // Um valor de limpeza por anexo (os de resolve/DONT_CARE são ignorados).
        VkClearValue clears[6]{};
        u32 n = 0;
        std::memcpy(clears[n++].color.float32, pass.clear, sizeof(f32) * 4);
        if (c1) std::memcpy(clears[n++].color.float32, pass.clear, sizeof(f32) * 4);
        clears[n++].depthStencil = {pass.clearDepth, 0};
        info.clearValueCount = n;
        info.pClearValues = clears;
        if (!info.renderPass || !info.framebuffer) return;
        vkCmdBeginRenderPass(cmd_, &info, VK_SUBPASS_CONTENTS_INLINE);
        inRenderPass_ = true;
        targetWidth_ = d->desc.width;
        targetHeight_ = d->desc.height;
        if (frame_) {
            for (Texture* t : {c0, c1, d, r0, r1, dr}) if (t) t->lastUsedFrame = frame_->frameNumber;
        }
        // O estado depois do passe: o resolve deixa os alvos de 1 amostra em
        // anexo (layout de anexo), como os próprios multiamostrados.
        set_viewport(0, 0, static_cast<f32>(targetWidth_), static_cast<f32>(targetHeight_));
        set_scissor(0, 0, targetWidth_, targetHeight_);
        return;
    }
    if (pass.depth.valid()) {
        // 3D: cor (opcional) + profundidade.
        Texture* d = backend_->texture(pass.depth.id);
        Texture* c = pass.color.valid() ? backend_->texture(pass.color.id) : nullptr;
        if (!d || (pass.color.valid() && !c)) return;
        if (c && c->state != ResourceState::ColorAttachment) {
            backend_->transition(cmd_, *c, ResourceState::ColorAttachment, pass.load != LoadOp::Load);
        }
        if (d->state != ResourceState::DepthAttachment) {
            backend_->transition(cmd_, *d, ResourceState::DepthAttachment, pass.depthLoad != LoadOp::Load);
        }
        VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        info.renderPass = backend_->render_pass(c ? c->format : VK_FORMAT_UNDEFINED, pass.load, d->format,
                                                pass.depthLoad, pass.storeDepth);
        info.framebuffer = backend_->framebuffer(c, *d, pass.depth.id, pass.load, pass.depthLoad, pass.storeDepth);
        info.renderArea = {{0, 0}, {d->desc.width, d->desc.height}};
        VkClearValue clears[2]{};
        u32 n = 0;
        if (c) {
            std::memcpy(clears[n].color.float32, pass.clear, sizeof(f32) * 4);
            ++n;
        }
        clears[n].depthStencil = {pass.clearDepth, 0};
        ++n;
        info.clearValueCount = n;
        info.pClearValues = clears;
        if (!info.renderPass || !info.framebuffer) return;
        vkCmdBeginRenderPass(cmd_, &info, VK_SUBPASS_CONTENTS_INLINE);
        inRenderPass_ = true;
        targetWidth_ = d->desc.width;
        targetHeight_ = d->desc.height;
        if (frame_) {
            d->lastUsedFrame = frame_->frameNumber;
            if (c) c->lastUsedFrame = frame_->frameNumber;
        }
        set_viewport(0, 0, static_cast<f32>(targetWidth_), static_cast<f32>(targetHeight_));
        set_scissor(0, 0, targetWidth_, targetHeight_);
        return;
    }
    Texture* t = backend_->texture(pass.color.id);
    if (!t) return;
    // O grafo já deixou o alvo em ColorAttachment; se alguém chamar direto, a
    // transição fica garantida aqui.
    if (t->state != ResourceState::ColorAttachment) {
        backend_->transition(cmd_, *t, ResourceState::ColorAttachment, pass.load != LoadOp::Load);
    }
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = backend_->render_pass(t->format, pass.load);
    info.framebuffer = backend_->framebuffer(*t, pass.load);
    info.renderArea = {{0, 0}, {t->desc.width, t->desc.height}};
    VkClearValue clear{};
    std::memcpy(clear.color.float32, pass.clear, sizeof(f32) * 4);
    info.clearValueCount = pass.load == LoadOp::Clear ? 1 : 0;
    info.pClearValues = &clear;
    if (!info.renderPass || !info.framebuffer) return;
    vkCmdBeginRenderPass(cmd_, &info, VK_SUBPASS_CONTENTS_INLINE);
    inRenderPass_ = true;
    targetWidth_ = t->desc.width;
    targetHeight_ = t->desc.height;
    t->lastUsedFrame = frame_ ? frame_->frameNumber : 0;
    set_viewport(0, 0, static_cast<f32>(targetWidth_), static_cast<f32>(targetHeight_));
    set_scissor(0, 0, targetWidth_, targetHeight_);
}

void CommandListImpl::end_render_pass() noexcept {
    if (!inRenderPass_) return;
    vkCmdEndRenderPass(cmd_);
    inRenderPass_ = false;
}

void CommandListImpl::bind_pipeline(PipelineHandle pipeline) noexcept {
    if (!cmd_) return;
    const PipelineObject* p = backend_->pipeline(pipeline.id);
    if (!p) return;
    if (p != pipeline_) {
        vkCmdBindPipeline(cmd_, p->bindPoint, p->pipeline);
        // Layout diferente invalida o set amarrado.
        if (!pipeline_ || pipeline_->layout != p->layout) dirty_ = true;
        pipeline_ = p;
    }
}

void CommandListImpl::bind_texture(u32 slot, TextureHandle texture, SamplerHandle sampler) noexcept {
    if (slot >= binding::kTextureSlots) return;
    textures_[slot] = TexBinding{texture.id, sampler.id};
    dirty_ = true;
}

void CommandListImpl::bind_storage_image(u32 slot, TextureHandle texture) noexcept {
    if (slot >= binding::kStorageImageSlots) return;
    storageImages_[slot] = texture.id;
    dirty_ = true;
}

void CommandListImpl::bind_storage_buffer(BufferHandle buffer) noexcept {
    storageBuffer_ = buffer.id;
    dirty_ = true;
}

void CommandListImpl::bind_storage_buffer_at(u32 slot, BufferHandle buffer) noexcept {
    if (slot == 0) storageBuffer_ = buffer.id;
    else if (slot == 1) storageBuffer1_ = buffer.id;
    else return;
    dirty_ = true;
}

void CommandListImpl::set_uniforms(const void* data, u32 bytes) noexcept {
    if (!frame_ || !data || bytes == 0) return;
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceSize off = 0;
    void* ptr = nullptr;
    if (!frame_->uniforms.allocate(bytes, backend_->uniform_alignment(), buf, off, ptr)) {
        (void)backend_->fail_recording(VK_ERROR_OUT_OF_DEVICE_MEMORY, "frame uniform allocation");
        return;
    }
    std::memcpy(ptr, data, bytes);
    if (buf != uniformBuffer_ || bytes != uniformSize_) dirty_ = true;
    uniformBuffer_ = buf;
    uniformOffset_ = static_cast<u32>(off);
    uniformSize_ = bytes;
}

void CommandListImpl::push_constants(const void* data, u32 bytes) noexcept {
    if (!cmd_) return;
    if (!pipeline_ || !data || !bytes) return;
    const u32 count = std::min(bytes, binding::kPushConstantBytes);
    std::memcpy(pushData_, data, count);
    pushBytes_ = std::max(pushBytes_, count);
    pushLayout_ = pipeline_->layout;
    vkCmdPushConstants(cmd_, pipeline_->layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                       0, std::min(bytes, binding::kPushConstantBytes), data);
}

void CommandListImpl::set_viewport(f32 x, f32 y, f32 w, f32 h) noexcept {
    if (!cmd_) return;
    VkViewport v{x, y, w, h, 0.0f, 1.0f};
    viewport_ = v; hasViewport_ = true;
    vkCmdSetViewport(cmd_, 0, 1, &v);
}

void CommandListImpl::set_scissor(i32 x, i32 y, u32 w, u32 h) noexcept {
    if (!cmd_) return;
    VkRect2D r{{x, y}, {w, h}};
    scissor_ = r; hasScissor_ = true;
    vkCmdSetScissor(cmd_, 0, 1, &r);
}

bool CommandListImpl::flush_descriptors() noexcept {
    if (!pipeline_) return false;
    if (!dirty_) {
        // Só o deslocamento do uniform mudou: reamarra o mesmo set com o novo
        // deslocamento dinâmico (barato: nada é escrito no set).
        return true;
    }
    VkDescriptorSet set = backend_->allocate_set(*frame_, pipeline_->setLayout);
    if (!set) {
        (void)backend_->fail_recording(VK_ERROR_OUT_OF_DEVICE_MEMORY, "frame descriptor allocation");
        return false;
    }

    VkDescriptorImageInfo images[binding::kTextureSlots]{};
    VkDescriptorImageInfo storage[binding::kStorageImageSlots]{};
    VkDescriptorBufferInfo ubo{};
    VkDescriptorBufferInfo ssbo{};
    VkDescriptorBufferInfo ssbo1{};
    VkWriteDescriptorSet writes[binding::kBindingCount]{};
    u32 n = 0;

    Texture& dummy = backend_->dummy_texture();
    for (u32 i = 0; i < binding::kTextureSlots; ++i) {
        Texture* t = textures_[i].texture ? backend_->texture(textures_[i].texture) : nullptr;
        const SamplerObject* s = textures_[i].sampler ? backend_->sampler(textures_[i].sampler) : nullptr;
        images[i].imageView = t ? t->view : dummy.view;
        images[i].imageLayout = t && t->layout == VK_IMAGE_LAYOUT_GENERAL ? VK_IMAGE_LAYOUT_GENERAL
                                                                         : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        images[i].sampler = s ? s->sampler : backend_->default_sampler();
        if (t) t->lastUsedFrame = frame_->frameNumber;
        writes[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[n].dstSet = set;
        writes[n].dstBinding = i;
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[n].pImageInfo = &images[i];
        ++n;
    }

    ubo.buffer = uniformBuffer_ ? uniformBuffer_ : backend_->dummy_buffer().buffer;
    ubo.offset = 0;
    ubo.range = uniformBuffer_ ? std::max<u32>(uniformSize_, 16) : 16;
    writes[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[n].dstSet = set;
    writes[n].dstBinding = binding::kUniform;
    writes[n].descriptorCount = 1;
    writes[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[n].pBufferInfo = &ubo;
    ++n;

    Texture& dummyStorage = backend_->dummy_storage();
    for (u32 i = 0; i < binding::kStorageImageSlots; ++i) {
        Texture* t = storageImages_[i] ? backend_->texture(storageImages_[i]) : nullptr;
        storage[i].imageView = t ? t->view : dummyStorage.view;
        storage[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        writes[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[n].dstSet = set;
        writes[n].dstBinding = binding::kStorageImage0 + i;
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[n].pImageInfo = &storage[i];
        ++n;
    }

    Buffer* sb = storageBuffer_ ? backend_->buffer(storageBuffer_) : nullptr;
    ssbo.buffer = sb ? sb->buffer : backend_->dummy_buffer().buffer;
    ssbo.offset = 0;
    ssbo.range = VK_WHOLE_SIZE;
    writes[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[n].dstSet = set;
    writes[n].dstBinding = binding::kStorageBuffer;
    writes[n].descriptorCount = 1;
    writes[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[n].pBufferInfo = &ssbo;
    ++n;

    Buffer* sb1 = storageBuffer1_ ? backend_->buffer(storageBuffer1_) : nullptr;
    ssbo1.buffer = sb1 ? sb1->buffer : backend_->dummy_buffer().buffer;
    ssbo1.offset = 0;
    ssbo1.range = VK_WHOLE_SIZE;
    writes[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[n].dstSet = set;
    writes[n].dstBinding = binding::kStorageBuffer1;
    writes[n].descriptorCount = 1;
    writes[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[n].pBufferInfo = &ssbo1;
    ++n;

    vkUpdateDescriptorSets(backend_->device(), n, writes, 0, nullptr);
    const u32 dynamicOffset = uniformBuffer_ ? uniformOffset_ : 0;
    vkCmdBindDescriptorSets(cmd_, pipeline_->bindPoint, pipeline_->layout, 0, 1, &set, 1, &dynamicOffset);
    dirty_ = false;
    lastSet_ = set;
    return true;
}

bool CommandListImpl::prepare_draw() noexcept {
    if (!inRenderPass_ || !pipeline_) return false;
    if (dirty_) return flush_descriptors();
    if (lastSet_) {
        const u32 dynamicOffset = uniformBuffer_ ? uniformOffset_ : 0;
        vkCmdBindDescriptorSets(cmd_, pipeline_->bindPoint, pipeline_->layout, 0, 1, &lastSet_, 1, &dynamicOffset);
    }
    return true;
}

void CommandListImpl::draw(u32 vertexCount, u32 instanceCount, u32 firstVertex) noexcept {
    if (!prepare_draw()) return;
    vkCmdDraw(cmd_, vertexCount, instanceCount, firstVertex, 0);
}

void CommandListImpl::bind_vertex_buffer(u32 binding, BufferHandle buffer, u64 offset) noexcept {
    if (!cmd_) return;
    Buffer* b = backend_->buffer(buffer.id);
    if (!b) return;
    const VkDeviceSize off = offset;
    if (vertices_.size() <= binding) vertices_.resize(static_cast<usize>(binding) + 1);
    vertices_[binding] = VertexBinding{b->buffer, off};
    vkCmdBindVertexBuffers(cmd_, binding, 1, &b->buffer, &off);
}

void CommandListImpl::bind_index_buffer(BufferHandle buffer, u64 offset, IndexType type) noexcept {
    if (!cmd_) return;
    Buffer* b = backend_->buffer(buffer.id);
    if (!b) return;
    indexBuffer_ = b->buffer; indexOffset_ = offset;
    indexType_ = type == IndexType::U16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(cmd_, b->buffer, offset, indexType_);
}

void CommandListImpl::draw_indexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset,
                                   u32 firstInstance) noexcept {
    if (!prepare_draw()) return;
    vkCmdDrawIndexed(cmd_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void CommandListImpl::dispatch(u32 x, u32 y, u32 z) noexcept {
    if (inRenderPass_ || !pipeline_ || pipeline_->bindPoint != VK_PIPELINE_BIND_POINT_COMPUTE) return;
    if (dirty_ && !flush_descriptors()) return;
    vkCmdDispatch(cmd_, x, y, z);
}

void CommandListImpl::copy_texture(TextureHandle src, TextureHandle dst) noexcept {
    if (!cmd_) return;
    Texture* s = backend_->texture(src.id);
    Texture* d = backend_->texture(dst.id);
    if (!s || !d || inRenderPass_) return;
    backend_->transition(cmd_, *s, ResourceState::TransferSrc, false);
    backend_->transition(cmd_, *d, ResourceState::TransferDst, true);
    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {std::min(s->desc.width, d->desc.width), std::min(s->desc.height, d->desc.height), 1};
    vkCmdCopyImage(cmd_, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void CommandListImpl::copy_texture_to_buffer(TextureHandle src, BufferHandle dst) noexcept {
    if (!cmd_) return;
    Texture* s = backend_->texture(src.id);
    Buffer* b = backend_->buffer(dst.id);
    if (!s || !b || inRenderPass_) return;
    backend_->transition(cmd_, *s, ResourceState::TransferSrc, false);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {s->desc.width, s->desc.height, 1};
    vkCmdCopyImageToBuffer(cmd_, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b->buffer, 1, &region);
    // A CPU lê o buffer depois do fence (export): a escrita da cópia tem de
    // ficar disponível para o domínio do host.
    VkBufferMemoryBarrier hb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hb.srcQueueFamilyIndex = hb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hb.buffer = b->buffer;
    hb.offset = 0;
    hb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hb, 0,
                         nullptr);
}

// Medição: um timestamp BOTTOM_OF_PIPE no começo e no fim de cada passe (fora
// de render pass, onde GPUs tile-based medem de forma confiável). BOTTOM nos
// dois lados: o início espera o passe anterior terminar, então os tempos não
// se sobrepõem e a soma bate com o total do frame.
void CommandListImpl::begin_timer(const char* label) noexcept {
    if (!frame_ || !frame_->queries || !backend_->timers_enabled() || inRenderPass_) return;
    const u32 index = static_cast<u32>(frame_->timerLabels.size());
    if (index >= backend_->max_timers()) return;
    frame_->timerLabels.push_back(label);
    frame_->timerStack.push_back(index);
    vkCmdWriteTimestamp(cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame_->queries, 2 + index * 2);
    frame_->timersWritten = true;
}

void CommandListImpl::end_timer() noexcept {
    if (!frame_ || !frame_->queries || frame_->timerStack.empty() || inRenderPass_) return;
    const u32 index = frame_->timerStack.back();
    frame_->timerStack.pop_back();
    vkCmdWriteTimestamp(cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame_->queries, 2 + index * 2 + 1);
}

void CommandListImpl::begin_label(const char* label) noexcept {
    if (!cmd_) return;
    if (!label) return;
    ++labelDepth_;
    if (!backend_->debug_utils() || !vkCmdBeginDebugUtilsLabelEXT) return;
    VkDebugUtilsLabelEXT l{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    l.pLabelName = label;
    vkCmdBeginDebugUtilsLabelEXT(cmd_, &l);
}

void CommandListImpl::end_label() noexcept {
    if (!cmd_) return;
    if (!labelDepth_) return;
    --labelDepth_;
    if (!backend_->debug_utils() || !vkCmdEndDebugUtilsLabelEXT) return;
    vkCmdEndDebugUtilsLabelEXT(cmd_);
}

Status CommandListImpl::finish_pass() noexcept {
    if (!backend_) return Status{Errc::InvalidState, "sem backend"};
    if (!backend_->recordingStatus_.ok()) return backend_->recordingStatus_;
    if (!frame_ || !cmd_) return Status{Errc::InvalidState, "sem frame"};
    ++frame_->passesInCommandBuffer;
    if (!backend_->passesPerCommandBuffer_
        || frame_->passesInCommandBuffer < backend_->passesPerCommandBuffer_
        || inRenderPass_ || labelDepth_ || !frame_->timerStack.empty()) return OkStatus;

    // A large primary buffer stalls some mobile drivers with many independent
    // 3D temporal passes. Split only recording, never quality or submissions.
    // The one final submit/fence owns every primary and all per-frame storage.
    FrameContext& f = *frame_;
    if (const VkResult r = vkEndCommandBuffer(cmd_); r != VK_SUCCESS)
        return backend_->fail_recording(r, "vkEndCommandBuffer checkpoint");
    if (f.commandBufferCount == f.commandBuffers.size()) {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer next = VK_NULL_HANDLE;
        if (const VkResult r = vkAllocateCommandBuffers(backend_->device(), &ai, &next); r != VK_SUCCESS)
            return backend_->fail_recording(r, "vkAllocateCommandBuffers checkpoint");
        f.commandBuffers.push_back(next);
    }
    f.cmd = f.commandBuffers[f.commandBufferCount++];
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (const VkResult r = vkBeginCommandBuffer(f.cmd, &bi); r != VK_SUCCESS)
        return backend_->fail_recording(r, "vkBeginCommandBuffer checkpoint");
    cmd_ = f.cmd;
    f.passesInCommandBuffer = 0;

    // Layouts use the same complete push-constant range. Preserve the tail
    // after a caller overwrites only a prefix, as the unsplit buffer does.
    if (pushLayout_ && pushBytes_)
        vkCmdPushConstants(cmd_, pushLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                           | VK_SHADER_STAGE_COMPUTE_BIT, 0, pushBytes_, pushData_);
    if (pipeline_) {
        vkCmdBindPipeline(cmd_, pipeline_->bindPoint, pipeline_->pipeline);
        if (!dirty_ && lastSet_) {
            const u32 offset = uniformBuffer_ ? uniformOffset_ : 0;
            vkCmdBindDescriptorSets(cmd_, pipeline_->bindPoint, pipeline_->layout,
                                    0, 1, &lastSet_, 1, &offset);
        }
    }
    for (u32 i = 0; i < vertices_.size(); ++i) if (vertices_[i].buffer)
        vkCmdBindVertexBuffers(cmd_, i, 1, &vertices_[i].buffer, &vertices_[i].offset);
    if (indexBuffer_) vkCmdBindIndexBuffer(cmd_, indexBuffer_, indexOffset_, indexType_);
    if (hasViewport_) vkCmdSetViewport(cmd_, 0, 1, &viewport_);
    if (hasScissor_) vkCmdSetScissor(cmd_, 0, 1, &scissor_);
    return OkStatus;
}

// =============================================================================
// Frame
// =============================================================================
void Backend::run_deferred(FrameContext& f) noexcept {
    // Cópia antes de rodar: uma destruição pode, em cascata, adiar outra.
    std::vector<DeferredRelease> list;
    list.swap(f.deferred);
    for (const DeferredRelease& d : list) if (d.fn) d.fn(d.ctx);
    list.clear();
    if (f.deferred.empty()) f.deferred.swap(list);   // devolve a capacidade
}

void Backend::collect_timings(FrameContext& f) noexcept {
    if (!f.timersWritten || !f.queries) return;
    const u32 count = static_cast<u32>(f.timerLabels.size());
    const u32 queries = 2 + count * 2;
    u64 values[2 + kMaxTimers * 2]{};
    const VkResult r = vkGetQueryPoolResults(device_, f.queries, 0, queries, sizeof(values), values,
                                             sizeof(u64), VK_QUERY_RESULT_64_BIT);
    f.timersWritten = false;
    if (r != VK_SUCCESS) return;
    const f64 toMs = static_cast<f64>(timestampPeriod_) * 1e-6;
    timings_.clear();
    for (u32 i = 0; i < count; ++i) {
        const u64 a = values[2 + i * 2] & timestampMask_;
        const u64 b = values[2 + i * 2 + 1] & timestampMask_;
        GpuTiming t;
        t.label = f.timerLabels[i];
        t.id = i;
        t.ms = b >= a ? static_cast<f32>(static_cast<f64>(b - a) * toMs) : 0.0f;
        timings_.push_back(t);
    }
    const u64 start = values[0] & timestampMask_;
    const u64 end = values[1] & timestampMask_;
    timingTotalMs_ = end >= start ? static_cast<f32>(static_cast<f64>(end - start) * toMs) : 0.0f;
    timingsValid_ = true;
}

u32 Backend::read_gpu_timings(GpuTiming* out, u32 capacity, f32* totalMs) noexcept {
    if (!timingsValid_) return 0;
    const u32 n = std::min<u32>(capacity, static_cast<u32>(timings_.size()));
    for (u32 i = 0; i < n; ++i) out[i] = timings_[i];
    if (totalMs) *totalMs = timingTotalMs_;
    return n;
}

Status Backend::fail_recording(VkResult result, const char* operation) noexcept {
    if (recordingStatus_.ok()) {
        recordingStatus_ = check(result, operation);
        (void)note_device_lost(result);
        AUREA_LOG_ERROR("vulkan: recording stopped until reinitialize: %s (%s)", operation, result_name(result));
    }
    // Nothing from this frame was submitted. Its tracked layouts may already
    // have changed, so do not reuse them. Keep deferred releases until a real
    // wait_idle/shutdown drains older submissions; never fake device loss.
    if (current_) current_->timersWritten = false;
    current_ = nullptr;
    commands_.bind_frame(this, nullptr, VK_NULL_HANDLE);
    return recordingStatus_;
}

Status Backend::begin_frame(FrameBegin& out) noexcept {
    return begin_frame_impl(out, true);
}

Status Backend::begin_offscreen_frame(FrameBegin& out) noexcept {
    return begin_frame_impl(out, false);
}

Status Backend::begin_frame_impl(FrameBegin& out, bool withSurface) noexcept {
    out = FrameBegin{};
    if (!initialized_) return Status{Errc::InvalidState, "backend nao inicializado"};
    if (deviceLost_) return Status{Errc::DeviceLost, "dispositivo perdido"};
    if (!recordingStatus_.ok()) return recordingStatus_;
    if (current_) return Status{Errc::InvalidState, "frame ja aberto"};
    collect_immediate();
    // A finished slot may not be reused for two more frames. Retire its old
    // allocations now, before the next frame allocates replacement targets.
    for (u32 i = 0; i < framesInFlight_; ++i) {
        FrameContext& done = frames_[i];
        if (done.submitted && !done.deferred.empty()
                && vkWaitForFences(device_, 1, &done.fence, VK_TRUE, 0) == VK_SUCCESS)
            run_deferred(done);
    }

    FrameContext& f = frames_[frameCursor_];
    if (f.submitted) {
        const VkResult w = vkWaitForFences(device_, 1, &f.fence, VK_TRUE, 2'000'000'000ull);
        if (note_device_lost(w)) return Status{Errc::DeviceLost, "fence"};
        if (w == VK_TIMEOUT) return Status{Errc::Timeout, "GPU atrasada: frame pulado"};
        if (w != VK_SUCCESS) return fail_recording(w, "vkWaitForFences frame");
        collect_timings(f);
        run_deferred(f);
        // Imagens importadas que ninguém usa há 10 s: a importação é soltada
        // (o decoder pode ter trocado de buffers após um seek ou fechamento).
        if (frameNumber_ % 120 == 0) {
            std::vector<u64> stale;
            for (auto& [buf, id] : importedByBuffer_) {
                const Texture* t = textures_.get(id);
                if (!t || frameNumber_ > t->lastUsedFrame + 600) stale.push_back(id);
            }
            for (u64 id : stale) destroy_texture(TextureHandle{id});
        }
    }
    f.submitted = false;
    f.frameNumber = ++frameNumber_;
    for (VkDescriptorPool p : f.descriptorPools)
        if (const VkResult r = vkResetDescriptorPool(device_, p, 0); r != VK_SUCCESS)
            return fail_recording(r, "vkResetDescriptorPool frame");
    f.descriptorPoolCursor = 0;
    f.uniforms.reset();
    f.staging.reset();
    f.timerLabels.clear();
    f.timerStack.clear();
    f.externalAcquired.clear();
    uploadBytesFrame_ = 0;

    f.cmd = f.commandBuffers.front();
    f.commandBufferCount = 1;
    f.passesInCommandBuffer = 0;
    if (const VkResult r = vkResetCommandPool(device_, f.pool, 0); r != VK_SUCCESS)
        return fail_recording(r, "vkResetCommandPool");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (const VkResult r = vkBeginCommandBuffer(f.cmd, &bi); r != VK_SUCCESS)
        return fail_recording(r, "vkBeginCommandBuffer frame");
    if (f.queries && timersEnabled_) {
        vkCmdResetQueryPool(f.cmd, f.queries, 0, f.queryCount);
        vkCmdWriteTimestamp(f.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f.queries, 0);
    }
    current_ = &f;
    commands_.bind_frame(this, &f, f.cmd);

    out.commands = &commands_;
    out.frameNumber = frameNumber_;

    imageAcquired_ = false;
    // Frame offscreen (prévia de efeito, export): nada de swapchain. A imagem
    // da tela não é nossa, e apresentar daqui seria apresentar o quadro errado.
    if (!withSurface) return OkStatus;
    if (swapchain_ || swapchainDirty_) {
        // SUBOPTIMAL não é "recrie já": o Android o devolve A CADA apresentação
        // quando a pré-rotação escolhida difere da dica da janela (transform
        // sem suporte, espelhado...). Recriar a cada quadro = vkDeviceWaitIdle
        // + buffers novos e vazios por quadro — o preview PISCA. Recria só se a
        // superfície mudou de fato (rotação); tamanho chega pelo resize_surface.
        if (swapchainSuboptimal_ && !swapchainDirty_ && surface_ && frameNumber_ >= suboptimalCheckFrame_) {
            swapchainSuboptimal_ = false;
            suboptimalCheckFrame_ = frameNumber_ + 30;
            VkSurfaceCapabilitiesKHR caps{};
            if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps) == VK_SUCCESS
                && caps.currentTransform != swapCapsTransform_) {
                swapchainDirty_ = true;
            } else if (suboptimalIgnored_++ == 0) {
                AUREA_LOG_WARN("swapchain: SUBOPTIMAL sem mudanca na superficie; mantido (sem recriar a cada quadro)");
            }
        }
        if (swapchainDirty_ && surface_) {
            if (const Status s = recreate_swapchain(); !s.ok()) {
                AUREA_LOG_WARN("swapchain nao recriado: %s", s.message().data());
            }
        }
        if (swapchain_) {
            u32 index = 0;
            VkResult r = vkAcquireNextImageKHR(device_, swapchain_, 1'000'000'000ull, f.acquired,
                                               VK_NULL_HANDLE, &index);
            if (r == VK_ERROR_OUT_OF_DATE_KHR) {
                if (recreate_swapchain().ok()) {
                    r = vkAcquireNextImageKHR(device_, swapchain_, 1'000'000'000ull, f.acquired,
                                              VK_NULL_HANDLE, &index);
                }
            }
            if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
                if (r == VK_SUBOPTIMAL_KHR) swapchainSuboptimal_ = true;
                imageAcquired_ = true;
                imageIndex_ = index;
                Texture* t = textures_.get(swapTextures_[index]);
                // Conteúdo anterior do swapchain não interessa: o passe de
                // saída limpa. UNDEFINED evita uma leitura inútil da imagem.
                t->state = ResourceState::Undefined;
                t->layout = VK_IMAGE_LAYOUT_UNDEFINED;
                out.backbuffer = TextureHandle{swapTextures_[index]};
                out.backbufferWidth = swapExtent_.width;
                out.backbufferHeight = swapExtent_.height;
                out.backbufferFormat = from_vk(swapFormat_);
                switch (swapTransform_) {
                    case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:  out.rotation = SurfaceRotation::Rotate90; break;
                    case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: out.rotation = SurfaceRotation::Rotate180; break;
                    case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: out.rotation = SurfaceRotation::Rotate270; break;
                    default: out.rotation = SurfaceRotation::None; break;
                }
            } else if (note_device_lost(r)) {
                return Status{Errc::DeviceLost, "aquisicao"};
            } else {
                // Sem imagem neste vsync (superfície sumindo, timeout): o frame
                // segue sem backbuffer — gravado e submetido vazio, o que mantém
                // os fences e a fila de destruição andando.
                AUREA_LOG_WARN("swapchain: aquisicao falhou (%s)", result_name(r));
                swapchainDirty_ = r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR;
            }
        }
    }
    return OkStatus;
}

Status Backend::end_frame() noexcept {
    if (!recordingStatus_.ok()) return recordingStatus_;
    if (!current_) return Status{Errc::InvalidState, "nenhum frame aberto"};
    FrameContext& f = *current_;
    commands_.end_render_pass();

    // Devolve à fila estrangeira os frames do decoder lidos neste frame.
    for (u64 id : f.externalAcquired) {
        Texture* t = textures_.get(id);
        if (!t || !t->acquiredThisFrame) continue;
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = 0;
        b.dstAccessMask = 0;
        b.oldLayout = t->layout;
        b.newLayout = t->layout;
        b.srcQueueFamilyIndex = graphicsFamily_;
        b.dstQueueFamilyIndex = foreign_family(hasForeignQueue_);
        b.image = t->image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(f.cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        t->acquiredThisFrame = false;
        t->state = ResourceState::Undefined;
        t->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    Texture* back = imageAcquired_ ? textures_.get(swapTextures_[imageIndex_]) : nullptr;
    if (back && back->state != ResourceState::Present) {
        // Rede de segurança: o grafo termina a saída em Present; se nada
        // desenhou (layer vazia, erro), a imagem ainda precisa do layout certo.
        transition(f.cmd, *back, ResourceState::Present, back->state == ResourceState::Undefined);
    }
    if (f.queries && timersEnabled_) {
        vkCmdWriteTimestamp(f.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f.queries, 1);
        f.timersWritten = true;
    }
    if (const VkResult r = vkEndCommandBuffer(f.cmd); r != VK_SUCCESS)
        return fail_recording(r, "vkEndCommandBuffer frame");

    if (const VkResult r = vkResetFences(device_, 1, &f.fence); r != VK_SUCCESS)
        return fail_recording(r, "vkResetFences frame");
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = f.commandBufferCount;
    si.pCommandBuffers = f.commandBuffers.data();
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (imageAcquired_) {
        // Espera a imagem só no estágio de SAÍDA DE COR: os passes anteriores
        // (conversão de vídeo, efeitos) começam antes de o display liberar a
        // imagem — é paralelismo de graça.
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &f.acquired;
        si.pWaitDstStageMask = &waitStage;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &renderDone_[imageIndex_];
    }
    const VkResult r = vkQueueSubmit(queue_, 1, &si, f.fence);
    if (r != VK_SUCCESS) return fail_recording(r, "vkQueueSubmit frame");
    current_ = nullptr;
    commands_.bind_frame(this, nullptr, VK_NULL_HANDLE);
    frameCursor_ = (frameCursor_ + 1) % framesInFlight_;
    f.submitted = true;
    lastSubmitted_ = &f;

    if (imageAcquired_) {
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &renderDone_[imageIndex_];
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain_;
        pi.pImageIndices = &imageIndex_;
        const VkResult pr = vkQueuePresentKHR(queue_, &pi);
        imageAcquired_ = false;
        if (pr == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchainDirty_ = true;
        } else if (pr == VK_SUBOPTIMAL_KHR) {
            swapchainSuboptimal_ = true;   // conferido no próximo begin_frame
        } else if (pr == VK_ERROR_SURFACE_LOST_KHR) {
            swapchainDirty_ = true;
            return Status{Errc::SurfaceLost, "superficie perdida"};
        } else if (note_device_lost(pr)) {
            return Status{Errc::DeviceLost, "apresentacao"};
        }
    }
    return OkStatus;
}

// =============================================================================
// Destruição adiada e sincronização
// =============================================================================
FrameContext* Backend::deferral_target() noexcept {
    if (current_) return current_;
    if (lastSubmitted_ && lastSubmitted_->submitted) return lastSubmitted_;
    return nullptr;
}

void Backend::defer_until_gpu_done(void (*fn)(void*), void* ctx) noexcept {
    if (!fn) return;
    // Outside a frame, a timed-out immediate upload is newer than the last
    // normal submission. Its resources cannot retire on that older fence.
    if (!current_ && !deviceLost_ && !pendingImmediate_.empty()) {
        pendingImmediate_.back().deferred.push_back(DeferredRelease{fn, ctx});
        return;
    }
    FrameContext* f = deferral_target();
    if (!f || deviceLost_) {
        // Nenhum trabalho de GPU pendente (ou o dispositivo morreu e nada mais
        // vai executar): liberar agora é seguro.
        fn(ctx);
        return;
    }
    f->deferred.push_back(DeferredRelease{fn, ctx});
}

void Backend::wait_idle() noexcept {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    collect_immediate(true);
    for (u32 i = 0; i < framesInFlight_; ++i) {
        if (&frames_[i] == current_) continue;
        if (frames_[i].submitted) collect_timings(frames_[i]);
        run_deferred(frames_[i]);
    }
    allocator_.trim_empty_blocks();
}

u64 Backend::last_submitted_frame() const noexcept {
    return lastSubmitted_ && lastSubmitted_->submitted ? lastSubmitted_->frameNumber : 0;
}

Status Backend::wait_frame(u64 frameNumber, u64 timeoutNs) noexcept {
    if (!device_) return Status{Errc::InvalidState, "backend nao inicializado"};
    if (deviceLost_) return Status{Errc::DeviceLost, "dispositivo perdido"};
    collect_immediate();
    for (u32 i = 0; i < framesInFlight_; ++i) {
        FrameContext& f = frames_[i];
        if (&f == current_ || !f.submitted || f.frameNumber != frameNumber) continue;
        const VkResult w = vkWaitForFences(device_, 1, &f.fence, VK_TRUE, timeoutNs);
        if (note_device_lost(w)) return Status{Errc::DeviceLost, "fence"};
        if (w == VK_TIMEOUT) return Status{Errc::Timeout, "GPU atrasada"};
        const Status status = check(w, "vkWaitForFences");
        if (!status.ok()) return status;
        collect_immediate();
        // One queue: this fence also covers earlier submissions. Release
        // decoder image leases now, even if export cannot submit another frame.
        for (u32 j = 0; j < framesInFlight_; ++j) {
            FrameContext& done = frames_[j];
            if (&done != current_ && done.submitted && done.frameNumber <= frameNumber)
                run_deferred(done);
        }
        return OkStatus;
    }
    return OkStatus;
}

GpuMemoryStats Backend::memory_stats() const noexcept {
    GpuMemoryStats s;
    s.reservedBytes = allocator_.reserved_bytes();
    s.usedBytes = allocator_.used_bytes();
    s.blockCount = allocator_.block_count();
    s.allocationCount = allocator_.allocation_count();
    s.textureCount = textures_.count();
    s.bufferCount = buffers_.count();
    s.uploadBytesThisFrame = uploadBytesFrame_;
    return s;
}

} // namespace aurea::vk

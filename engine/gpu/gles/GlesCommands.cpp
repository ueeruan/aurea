#include "GlesInternal.hpp"

namespace aurea::gles {
namespace {
GLenum topology(Topology t) {
    return t == Topology::TriangleStrip ? GL_TRIANGLE_STRIP : t == Topology::PointList ? GL_POINTS
         : t == Topology::LineList ? GL_LINES : GL_TRIANGLES;
}
}
void Backend::Impl::attach(GLenum target, Texture* color, Texture* depth) {
    glFramebufferTexture2D(target, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color ? color->id : 0, 0);
    glFramebufferTexture2D(target, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth ? depth->id : 0, 0);
    if (target == GL_READ_FRAMEBUFFER) glReadBuffer(color ? GL_COLOR_ATTACHMENT0 : GL_NONE);
    else { const GLenum draw = color ? GL_COLOR_ATTACHMENT0 : GL_NONE; glDrawBuffers(1, &draw); }
    if (glCheckFramebufferStatus(target) != GL_FRAMEBUFFER_COMPLETE) fail(Errc::UnsupportedFormat, "GLES framebuffer incomplete");
}
void Backend::Impl::barrier(TextureHandle h, ResourceState state, bool) noexcept {
    auto* t = find(textures, h.id); if (!t) { fail(Errc::InvalidArgument, "GLES barrier references missing texture"); return; }
    if (t->state == ResourceState::StorageWrite || t->state == ResourceState::StorageReadWrite)
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
    t->state = state;
}
void Backend::Impl::begin_render_pass(const RenderPassBegin& begin) noexcept {
    pass = begin;
    auto* color = find(textures, pass.color.id); auto* depth = find(textures, pass.depth.id);
    if (!color && !depth) { fail(Errc::InvalidArgument, "GLES render pass has no target"); return; }
    targetWidth = color ? color->desc.width : depth->desc.width;
    targetHeight = color ? color->desc.height : depth->desc.height;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer); attach(GL_DRAW_FRAMEBUFFER, color, depth);
    glEnable(GL_SCISSOR_TEST); glScissor(0, 0, targetWidth, targetHeight); glViewport(0, 0, targetWidth, targetHeight);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE);
    if (color && pass.load == LoadOp::Clear) glClearBufferfv(GL_COLOR, 0, pass.clear);
    if (depth && pass.depthLoad == LoadOp::Clear) glClearBufferfv(GL_DEPTH, 0, &pass.clearDepth);
    GLenum discard[2]; GLsizei count = 0;
    if (color && pass.load == LoadOp::DontCare) discard[count++] = GL_COLOR_ATTACHMENT0;
    if (depth && pass.depthLoad == LoadOp::DontCare) discard[count++] = GL_DEPTH_ATTACHMENT;
    if (count) glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, count, discard);
    (void)check("begin_render_pass");
}
void Backend::Impl::end_render_pass() noexcept {
    if (pass.depth.valid() && !pass.storeDepth) { const GLenum discard = GL_DEPTH_ATTACHMENT; glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, 1, &discard); }
    (void)check("end_render_pass");
}
void Backend::Impl::bind_pipeline(PipelineHandle h) noexcept {
    pipeline = find(pipelines, h.id);
    if (!pipeline) { fail(Errc::InvalidArgument, "GLES missing pipeline"); return; }
    const auto& p = pipeline->desc;
    glUseProgram(pipeline->id);
    for (u32 slot = 0; slot < binding::kTextureSlots; ++slot) update_border(slot);
    if (p.isCompute) return;
    if (p.blendEnabled) { glEnable(GL_BLEND); glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_ONE, p.blend == BlendMode::Add ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA,
                           GL_ONE, p.blend == BlendMode::Add ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA); }
    else glDisable(GL_BLEND);
    if (p.depth.test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    constexpr GLenum comparisons[] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
    glDepthFunc(comparisons[static_cast<u32>(p.depth.compare)]); glDepthMask(p.depth.write ? GL_TRUE : GL_FALSE);
    if (p.cull == CullMode::None) glDisable(GL_CULL_FACE);
    else { glEnable(GL_CULL_FACE); glCullFace(p.cull == CullMode::Back ? GL_BACK : GL_FRONT); }
    // Offscreen row zero is the renderer's top row. This reverses GL's
    // bottom-left window-space winding relative to the shared pipeline contract.
    glFrontFace(p.frontFaceCCW ? GL_CW : GL_CCW);
    if (p.depth.biasConstant != 0 || p.depth.biasSlope != 0) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(p.depth.biasSlope, p.depth.biasConstant); }
    else glDisable(GL_POLYGON_OFFSET_FILL);
    (void)check("bind_pipeline");
}
void Backend::Impl::bind_texture(u32 slot, TextureHandle h, SamplerHandle sampler) noexcept {
    if (slot >= binding::kTextureSlots) { fail(Errc::OutOfRange, "GLES texture slot"); return; }
    auto* t = find(textures, h.id); auto* s = find(samplers, sampler.id);
    glActiveTexture(GL_TEXTURE0 + slot);
    // Keep both target types complete, including statically unused branches in
    // PBR shaders. A samplerCube and sampler2D may not alias a bound texture type.
    glBindTexture(GL_TEXTURE_2D, dummy2D); glBindTexture(GL_TEXTURE_CUBE_MAP, dummyCube);
    if (t) glBindTexture(t->target, t->id);
    glBindSampler(slot, s ? s->id : dummySampler);
    borderState[slot][0] = s ? s->borderMask : 0; borderState[slot][1] = s ? s->nearest : 0;
    if (s && s->borderMask && t && (t->desc.mipLevels > 1 || t->target != GL_TEXTURE_2D))
        fail(Errc::UnsupportedFeature, "GLES emulated transparent border needs a single-level 2D target");
    update_border(slot);
}
void Backend::Impl::update_border(u32 slot) {
    if (!pipeline) return;
    for (u32 stage = 0; stage < 3; ++stage) {
        const GLint location = pipeline->borderLocations[stage][slot];
        if (location >= 0) glUniform2iv(location, 1, borderState[slot]);
    }
}
void Backend::Impl::bind_storage_image(u32 slot, TextureHandle h) noexcept {
    auto* t = find(textures, h.id);
    if (slot >= binding::kStorageImageSlots || !t) { fail(Errc::InvalidArgument, "GLES storage image missing"); return; }
    glBindImageTexture(slot, t->id, 0, GL_FALSE, 0, GL_READ_WRITE, format_of(t->desc.format).internal);
}
void Backend::Impl::bind_storage_buffer_at(u32 slot, BufferHandle h) noexcept {
    if (slot >= binding::kStorageBufferSlots) { fail(Errc::OutOfRange, "GLES storage buffer slot"); return; }
    auto* b = find(buffers, h.id); glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, b ? b->id : dummyBuffer);
}
void Backend::Impl::uniforms(u32 bindingIndex, const void* data, u32 bytes) {
    if (!current || !data || !bytes || bytes > binding::kMaxUniformBytes) { fail(Errc::InvalidArgument, "GLES uniform range"); return; }
    constexpr u32 pageBytes = 256 * 1024;
    const u32 alignment = caps.minUniformBufferOffsetAlignment;
    const u32 padded = (bytes + 15) & ~15u;
    auto& f = *current;
    u32 offset = (f.offset + alignment - 1) / alignment * alignment;
    if (offset + padded > pageBytes) { ++f.page; offset = 0; }
    if (f.page == f.uniforms.size()) {
        UniformPage page; glGenBuffers(1, &page.id); glBindBuffer(GL_UNIFORM_BUFFER, page.id);
        glBufferData(GL_UNIFORM_BUFFER, pageBytes, nullptr, GL_STREAM_DRAW);
        if (!check("uniform page").ok()) { glDeleteBuffers(1, &page.id); return; }
        f.uniforms.push_back(page); bytesBuffers += pageBytes;
    }
    std::array<u8, binding::kMaxUniformBytes> paddedData{}; std::memcpy(paddedData.data(), data, bytes);
    glBindBuffer(GL_UNIFORM_BUFFER, f.uniforms[f.page].id);
    glBufferSubData(GL_UNIFORM_BUFFER, offset, padded, paddedData.data());
    glBindBufferRange(GL_UNIFORM_BUFFER, bindingIndex, f.uniforms[f.page].id, offset, padded);
    f.offset = offset + padded; uploadBytes += padded;
}
void Backend::Impl::vertices(i32 baseVertex) {
    if (!pipeline) return;
    const auto& layout = pipeline->desc.vertexLayout;
    for (u32 i = 0; i < caps.maxVertexInputAttributes; ++i) glDisableVertexAttribArray(i);
    for (u32 i = 0; i < layout.attributeCount; ++i) {
        const auto& a = layout.attributes[i]; const auto& binding = layout.bindings[a.binding];
        auto* b = find(buffers, vertexBuffers[a.binding]);
        if (!b) { fail(Errc::InvalidArgument, "GLES vertex buffer missing"); return; }
        GLenum type = GL_FLOAT; GLint components = 1; bool normalized = false, integer = false;
        switch (a.format) {
            case VertexFormat::Float: break;
            case VertexFormat::Float2: components = 2; break;
            case VertexFormat::Float3: components = 3; break;
            case VertexFormat::Float4: components = 4; break;
            case VertexFormat::UByte4Norm: components = 4; type = GL_UNSIGNED_BYTE; normalized = true; break;
            case VertexFormat::UShort2Norm: components = 2; type = GL_UNSIGNED_SHORT; normalized = true; break;
            case VertexFormat::UShort4Norm: components = 4; type = GL_UNSIGNED_SHORT; normalized = true; break;
            case VertexFormat::UByte4: components = 4; type = GL_UNSIGNED_BYTE; integer = true; break;
            case VertexFormat::UShort4: components = 4; type = GL_UNSIGNED_SHORT; integer = true; break;
            case VertexFormat::Short4Norm: components = 4; type = GL_SHORT; normalized = true; break;
        }
        const i64 offset = static_cast<i64>(vertexOffsets[a.binding]) + a.offset
                         + (binding.perInstance ? 0 : static_cast<i64>(baseVertex) * binding.stride);
        if (offset < 0 || static_cast<u64>(offset) >= b->desc.bytes) { fail(Errc::OutOfRange, "GLES vertex offset"); return; }
        glBindBuffer(GL_ARRAY_BUFFER, b->id); glEnableVertexAttribArray(a.location);
        const void* pointer = reinterpret_cast<const void*>(static_cast<uintptr_t>(offset));
        if (integer) glVertexAttribIPointer(a.location, components, type, binding.stride, pointer);
        else glVertexAttribPointer(a.location, components, type, normalized ? GL_TRUE : GL_FALSE, binding.stride, pointer);
        glVertexAttribDivisor(a.location, binding.perInstance ? 1 : 0);
    }
}
void Backend::Impl::draw(u32 count, u32 instances, u32 first) noexcept {
    if (!pipeline || !frameStatus.ok()) return;
    vertices(0); if (!frameStatus.ok()) return;
    glDrawArraysInstanced(topology(pipeline->desc.topology), first, count, instances); (void)check("draw");
}
void Backend::Impl::draw_indexed(u32 count, u32 instances, u32 first, i32 baseVertex, u32 firstInstance) noexcept {
    if (!pipeline || !frameStatus.ok()) return;
    if (firstInstance) { fail(Errc::NotSupported, "GLES base instance is unavailable"); return; }
    auto* b = find(buffers, indexBuffer); if (!b) { fail(Errc::InvalidArgument, "GLES index buffer missing"); return; }
    vertices(baseVertex); if (!frameStatus.ok()) return;
    const u32 bytes = indexType == IndexType::U16 ? 2 : 4;
    const u64 offset = indexOffset + static_cast<u64>(first) * bytes;
    if (offset > b->desc.bytes || static_cast<u64>(count) * bytes > b->desc.bytes - offset) { fail(Errc::OutOfRange, "GLES index range"); return; }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b->id);
    glDrawElementsInstanced(topology(pipeline->desc.topology), count, bytes == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                            reinterpret_cast<const void*>(static_cast<uintptr_t>(offset)), instances);
    (void)check("draw_indexed");
}
void Backend::Impl::copy_texture(TextureHandle source, TextureHandle destination) noexcept {
    auto* a = find(textures, source.id); auto* b = find(textures, destination.id);
    if (!a || !b) { fail(Errc::InvalidArgument, "GLES texture copy source/target missing"); return; }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer); attach(GL_READ_FRAMEBUFFER, a, nullptr);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer); attach(GL_DRAW_FRAMEBUFFER, b, nullptr);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, a->desc.width, a->desc.height, 0, 0, b->desc.width, b->desc.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glEnable(GL_SCISSOR_TEST); (void)check("copy_texture");
}
void Backend::Impl::copy_texture_to_buffer(TextureHandle source, BufferHandle destination) noexcept {
    auto* t = find(textures, source.id); auto* b = find(buffers, destination.id);
    if (!t || !b || b->desc.access != MemoryAccess::Readback) { fail(Errc::InvalidArgument, "GLES readback source/target missing"); return; }
    const auto f = format_of(t->desc.format);
    if (f.type != GL_UNSIGNED_BYTE) { fail(Errc::NotSupported, "GLES asynchronous readback requires byte color planes"); return; }
    const u32 components = f.external == GL_RED ? 1 : f.external == GL_RG ? 2 : 4;
    const usize bytes = static_cast<usize>(t->desc.width) * t->desc.height * 4;
    if (b->desc.bytes < bytes / 4 * components) { fail(Errc::OutOfRange, "GLES readback buffer too small"); return; }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, b->id);
    if (b->gpuBytes < bytes) { glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ); bytesBuffers += bytes - b->gpuBytes; b->gpuBytes = bytes; }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer); attach(GL_READ_FRAMEBUFFER, t, nullptr);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    // RGBA/UNSIGNED_BYTE is the portable ES read format for normalized targets;
    // map_buffer packs R8/RG8 into the stable CPU pointer held by the encoder.
    glReadPixels(0, 0, t->desc.width, t->desc.height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    b->readWidth = t->desc.width; b->readHeight = t->desc.height; b->readComponents = components;
    b->rgbaReadback = true; b->pendingRead = true; glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    (void)check("copy_texture_to_buffer");
}
void Backend::Impl::begin_timer(const char* label) noexcept {
    if (!caps.timestampQueries || !settings.enableGpuTimers || !current) return;
    auto& f = *current;
    const u32 index = f.timerCount++;
    if (index == f.timers.size()) { Timer timer; glGenQueries(1, &timer.begin); glGenQueries(1, &timer.end); f.timers.push_back(timer); }
    f.timers[index].label = label; f.timerStack.push_back(index); queryCounter(f.timers[index].begin, GL_TIMESTAMP_EXT);
}
void Backend::Impl::end_timer() noexcept {
    if (!caps.timestampQueries || !settings.enableGpuTimers || !current || current->timerStack.empty()) return;
    const u32 index = current->timerStack.back(); current->timerStack.pop_back(); queryCounter(current->timers[index].end, GL_TIMESTAMP_EXT);
}
} // namespace aurea::gles

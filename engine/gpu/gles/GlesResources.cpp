#include "GlesInternal.hpp"
#include "aurea/scene3d/Environment.hpp"
#include <cstdio>
#include <fstream>
#include <filesystem>

namespace aurea::gles {
Format format_of(SurfaceFormat f) noexcept {
    switch (f) {
        case SurfaceFormat::R8: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1};
        case SurfaceFormat::RG8: return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2};
        case SurfaceFormat::RGBA8: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
        case SurfaceFormat::RGBA8_sRGB: return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
        case SurfaceFormat::R16: return {GL_R16_EXT, GL_RED, GL_UNSIGNED_SHORT, 2};
        case SurfaceFormat::RG16: return {GL_RG16_EXT, GL_RG, GL_UNSIGNED_SHORT, 4};
        case SurfaceFormat::R16F: return {GL_R16F, GL_RED, GL_HALF_FLOAT, 2};
        case SurfaceFormat::RG16F: return {GL_RG16F, GL_RG, GL_HALF_FLOAT, 4};
        case SurfaceFormat::RGBA16F: return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8};
        case SurfaceFormat::R32F: return {GL_R32F, GL_RED, GL_FLOAT, 4};
        case SurfaceFormat::RGBA32F: return {GL_RGBA32F, GL_RGBA, GL_FLOAT, 16};
        case SurfaceFormat::Depth24: return {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 4};
        case SurfaceFormat::Depth32F: return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 4};
        case SurfaceFormat::BC7: return {GL_COMPRESSED_RGBA_BPTC_UNORM_EXT, 0, 0, 1, true};
        case SurfaceFormat::BC7_sRGB: return {GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT, 0, 0, 1, true};
        case SurfaceFormat::ETC2_RGBA8: return {GL_COMPRESSED_RGBA8_ETC2_EAC, 0, 0, 1, true};
        case SurfaceFormat::ETC2_RGBA8_sRGB: return {GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC, 0, 0, 1, true};
        case SurfaceFormat::ASTC4x4: return {GL_COMPRESSED_RGBA_ASTC_4x4_KHR, 0, 0, 1, true};
        case SurfaceFormat::ASTC4x4_sRGB: return {GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR, 0, 0, 1, true};
        case SurfaceFormat::BGRA8: return {};
    }
    return {};
}

Result<TextureHandle> Backend::create_texture(const TextureDesc& desc) noexcept {
    auto& d = *impl_; Impl::Scope scope(d);
    if (!scope.valid) return Status{Errc::InvalidState};
    if (!desc.width || !desc.height || !desc.depth || !desc.layers) return Status{Errc::InvalidArgument};
    if (desc.width > d.caps.maxTexture2D || desc.height > d.caps.maxTexture2D) return Status{Errc::OutOfRange};
    if (desc.sampleCount != 1) return Status{Errc::NotSupported, "GLES texture MSAA requires a separate resolve target"};
    if (desc.cube && (desc.width != desc.height || desc.layers != 6 || desc.depth != 1)) return Status{Errc::InvalidArgument};
    const Format f = format_of(desc.format);
    if (!f.internal) return Status{Errc::UnsupportedFormat};
    Texture t; t.desc = desc;
    t.target = desc.cube ? GL_TEXTURE_CUBE_MAP : desc.depth > 1 ? GL_TEXTURE_3D
             : desc.layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
    glGenTextures(1, &t.id); glBindTexture(t.target, t.id);
    if (t.target == GL_TEXTURE_3D || t.target == GL_TEXTURE_2D_ARRAY)
        glTexStorage3D(t.target, std::max(1u, desc.mipLevels), f.internal, desc.width, desc.height,
                       desc.depth > 1 ? desc.depth : desc.layers);
    else glTexStorage2D(t.target, std::max(1u, desc.mipLevels), f.internal, desc.width, desc.height);
    glTexParameteri(t.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(t.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(t.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(t.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(t.target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    const auto status = d.check("create_texture");
    if (!status.ok()) { glDeleteTextures(1, &t.id); return status; }
    const u64 id = d.nextId++; d.bytesTextures += desc.estimated_bytes();
    d.textures.emplace(id, std::move(t)); return TextureHandle{id};
}
void Backend::destroy_texture(TextureHandle h) noexcept {
    auto& d = *impl_; Impl::Scope scope(d);
    auto it = d.textures.find(h.id); if (!scope.valid || it == d.textures.end()) return;
    // GL retains objects referenced by queued commands until their execution.
    glDeleteTextures(1, &it->second.id);
    d.bytesTextures -= std::min(d.bytesTextures, it->second.desc.estimated_bytes());
    d.textures.erase(it);
}
TextureDesc Backend::texture_desc(TextureHandle h) const noexcept {
    auto* t = find(impl_->textures, h.id); return t ? t->desc : TextureDesc{};
}
Result<BufferHandle> Backend::create_buffer(const BufferDesc& desc) noexcept {
    auto& d = *impl_; Impl::Scope scope(d);
    if (!scope.valid) return Status{Errc::InvalidState};
    if (!desc.bytes) return Status{Errc::InvalidArgument};
    Buffer b; b.desc = desc; b.gpuBytes = desc.bytes;
    if (desc.access != MemoryAccess::GpuOnly) b.cpu.resize(desc.bytes);
    glGenBuffers(1, &b.id); glBindBuffer(GL_COPY_WRITE_BUFFER, b.id);
    glBufferData(GL_COPY_WRITE_BUFFER, desc.bytes, nullptr,
                 desc.access == MemoryAccess::Readback ? GL_STREAM_READ : GL_DYNAMIC_DRAW);
    const auto status = d.check("create_buffer");
    if (!status.ok()) { glDeleteBuffers(1, &b.id); return status; }
    const u64 id = d.nextId++; d.bytesBuffers += desc.bytes;
    d.buffers.emplace(id, std::move(b)); return BufferHandle{id};
}
void Backend::destroy_buffer(BufferHandle h) noexcept {
    auto& d = *impl_; Impl::Scope scope(d);
    auto it = d.buffers.find(h.id); if (!scope.valid || it == d.buffers.end()) return;
    glDeleteBuffers(1, &it->second.id); d.bytesBuffers -= it->second.gpuBytes; d.buffers.erase(it);
}
Status Backend::write_buffer(BufferHandle h, usize offset, const void* data, usize bytes) noexcept {
    auto& d = *impl_; Impl::Scope scope(d); auto* b = find(d.buffers, h.id);
    if (!scope.valid || !b) return Status{Errc::InvalidState};
    if (!data || offset > b->desc.bytes || bytes > b->desc.bytes - offset) return Status{Errc::OutOfRange};
    if (!b->cpu.empty()) std::memcpy(b->cpu.data() + offset, data, bytes);
    glBindBuffer(GL_COPY_WRITE_BUFFER, b->id); glBufferSubData(GL_COPY_WRITE_BUFFER, offset, bytes, data);
    d.uploadBytes += bytes; return d.check("write_buffer");
}
Status Backend::map_buffer(BufferHandle h, void*& pointer) noexcept {
    pointer = nullptr;
    auto& d = *impl_; Impl::Scope scope(d); auto* b = find(d.buffers, h.id);
    if (!scope.valid || !b) return Status{Errc::InvalidState};
    if (b->cpu.empty()) return Status{Errc::NotSupported};
    if (b->pendingRead) {
        glBindBuffer(GL_COPY_READ_BUFFER, b->id);
        const auto* data = static_cast<const u8*>(glMapBufferRange(GL_COPY_READ_BUFFER, 0, b->gpuBytes, GL_MAP_READ_BIT));
        if (!data) return Status{Errc::InvalidState, "GLES readback map failed"};
        if (b->rgbaReadback) {
            const usize pixels = static_cast<usize>(b->readWidth) * b->readHeight;
            for (usize i = 0; i < pixels; ++i)
                for (u32 c = 0; c < b->readComponents; ++c) b->cpu[i * b->readComponents + c] = data[i * 4 + c];
        } else std::memcpy(b->cpu.data(), data, b->desc.bytes);
        const GLboolean valid = glUnmapBuffer(GL_COPY_READ_BUFFER);
        if (!valid) return Status{Errc::DeviceLost};
        b->pendingRead = false;
    }
    pointer = b->cpu.data(); return d.check("map_buffer");
}
void Backend::unmap_buffer(BufferHandle h) noexcept {
    auto* b = find(impl_->buffers, h.id);
    if (b && b->desc.access == MemoryAccess::Upload && !b->cpu.empty())
        (void)write_buffer(h, 0, b->cpu.data(), b->cpu.size());
}

Result<SamplerHandle> Backend::create_sampler(const SamplerDesc& desc) noexcept {
    auto& d = *impl_; Impl::Scope scope(d); if (!scope.valid) return Status{Errc::InvalidState};
    const bool border = desc.wrapU == SamplerDesc::Wrap::ClampToBorder || desc.wrapV == SamplerDesc::Wrap::ClampToBorder;
    if (border && !d.borderClamp && (desc.minFilter != desc.magFilter || desc.maxAnisotropy > 1))
        return Status{Errc::UnsupportedFeature, "GLES emulated border requires matching filters and no anisotropy"};
    auto wrap = [&](SamplerDesc::Wrap w) { return w == SamplerDesc::Wrap::Repeat ? GL_REPEAT
        : w == SamplerDesc::Wrap::MirroredRepeat ? GL_MIRRORED_REPEAT
        : w == SamplerDesc::Wrap::ClampToBorder && d.borderClamp ? GL_CLAMP_TO_BORDER_EXT : GL_CLAMP_TO_EDGE; };
    Sampler s; glGenSamplers(1, &s.id);
    // Mipmap::Nearest still samples the base level for one-level textures.
    const GLenum min = desc.minFilter == SamplerDesc::Filter::Nearest
        ? (desc.mipmap == SamplerDesc::Mipmap::Linear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST)
        : (desc.mipmap == SamplerDesc::Mipmap::Linear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST);
    glSamplerParameteri(s.id, GL_TEXTURE_MIN_FILTER, min);
    glSamplerParameteri(s.id, GL_TEXTURE_MAG_FILTER, desc.magFilter == SamplerDesc::Filter::Nearest ? GL_NEAREST : GL_LINEAR);
    glSamplerParameteri(s.id, GL_TEXTURE_WRAP_S, wrap(desc.wrapU)); glSamplerParameteri(s.id, GL_TEXTURE_WRAP_T, wrap(desc.wrapV));
    glSamplerParameteri(s.id, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    if (border && d.borderClamp) { const GLfloat clear[4]{}; glSamplerParameterfv(s.id, GL_TEXTURE_BORDER_COLOR_EXT, clear); }
    if (border && !d.borderClamp) {
        s.borderMask = (desc.wrapU == SamplerDesc::Wrap::ClampToBorder ? 1 : 0) | (desc.wrapV == SamplerDesc::Wrap::ClampToBorder ? 2 : 0);
        s.nearest = desc.minFilter == SamplerDesc::Filter::Nearest ? 1 : 0;
    }
    if (d.caps.maxSamplerAnisotropy > 1) glSamplerParameterf(s.id, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::clamp(desc.maxAnisotropy, 1.0f, d.caps.maxSamplerAnisotropy));
    const auto status = d.check("create_sampler");
    if (!status.ok()) { glDeleteSamplers(1, &s.id); return status; }
    const u64 id = d.nextId++; d.samplers.emplace(id, s); return SamplerHandle{id};
}
void Backend::destroy_sampler(SamplerHandle h) noexcept {
    auto& d = *impl_; Impl::Scope scope(d); auto it = d.samplers.find(h.id);
    if (scope.valid && it != d.samplers.end()) { glDeleteSamplers(1, &it->second.id); d.samplers.erase(it); }
}
Result<ShaderHandle> Backend::create_shader(const ShaderDesc& desc) noexcept {
    auto& d = *impl_; Impl::Scope scope(d); if (!scope.valid) return Status{Errc::InvalidState};
    const char* source = shader_source(desc.spirv, desc.spirvBytes);
    if (!source) return Status{Errc::ShaderCompileFailed, "SPIR-V has no matching build-time ES translation"};
    Shader shader; shader.id = glCreateShader(desc.stage == ShaderStage::Vertex ? GL_VERTEX_SHADER
        : desc.stage == ShaderStage::Fragment ? GL_FRAGMENT_SHADER : GL_COMPUTE_SHADER);
    glShaderSource(shader.id, 1, &source, nullptr); glCompileShader(shader.id);
    GLint ok = 0; glGetShaderiv(shader.id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192]{}; glGetShaderInfoLog(shader.id, sizeof(log), nullptr, log);
        AUREA_LOG_ERROR("GLES shader %s: %s", desc.debugName ? desc.debugName : "", log);
        glDeleteShader(shader.id); return Status{Errc::ShaderCompileFailed};
    }
    shader.hash = 14695981039346656037ull;
    for (usize i = 0; i < desc.spirvBytes; ++i) { shader.hash ^= reinterpret_cast<const u8*>(desc.spirv)[i]; shader.hash *= 1099511628211ull; }
    // Translator fixes must invalidate binaries even when the SPIR-V is unchanged.
    for (const char* p = source; *p; ++p) { shader.hash ^= static_cast<u8>(*p); shader.hash *= 1099511628211ull; }
    const u64 id = d.nextId++; d.shaders.emplace(id, shader); return ShaderHandle{id};
}
void Backend::destroy_shader(ShaderHandle h) noexcept {
    auto& d = *impl_; Impl::Scope scope(d); auto it = d.shaders.find(h.id);
    if (scope.valid && it != d.shaders.end()) { glDeleteShader(it->second.id); d.shaders.erase(it); }
}
std::string Backend::Impl::cache_path(u64 key) const {
    if (cacheDirectory.empty()) return {};
    u64 driver = 14695981039346656037ull;
    for (u8 c : caps.deviceName + caps.driverInfo) { driver ^= c; driver *= 1099511628211ull; }
    char file[96]; std::snprintf(file, sizeof(file), "/gles-%016llx-%016llx.bin",
        static_cast<unsigned long long>(driver), static_cast<unsigned long long>(key));
    return cacheDirectory + file;
}
Result<PipelineHandle> Backend::create_pipeline(const PipelineDesc& desc) noexcept {
    auto& d = *impl_; Impl::Scope scope(d); if (!scope.valid) return Status{Errc::InvalidState};
    auto* a = find(d.shaders, desc.isCompute ? desc.computeShader.id : desc.vertexShader.id);
    auto* b = desc.isCompute ? nullptr : find(d.shaders, desc.fragmentShader.id);
    if (!a || (!desc.isCompute && !b)) return Status{Errc::InvalidArgument};
    Pipeline p; p.desc = desc; p.id = glCreateProgram();
    p.cacheKey = (a->hash * 1099511628211ull) ^ (b ? b->hash : 0);
    GLint linked = 0;
    const std::string path = d.cache_path(p.cacheKey);
    if (!path.empty()) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        const auto bytes = file.tellg();
        if (bytes > 8 && bytes < 16 * 1024 * 1024) {
            file.seekg(0); u32 header[2]{}; file.read(reinterpret_cast<char*>(header), sizeof(header));
            std::vector<char> binary(static_cast<usize>(bytes) - sizeof(header)); file.read(binary.data(), binary.size());
            if (file && header[0] == 0x314c4741u) {
                glProgramBinary(p.id, header[1], binary.data(), binary.size());
                glGetProgramiv(p.id, GL_LINK_STATUS, &linked);
                // A driver may reject an old binary; compile the original source.
                if (!linked) {
                    const GLenum error = glGetError();
                    AUREA_LOG_WARN("GLES cached binary rejected (0x%x); compiling shader source", error);
                    while (glGetError() != GL_NO_ERROR) {}
                    d.cacheInfo.load = PipelineCacheInfo::Load::Rejected;
                }
                else { p.persisted = true; d.cacheInfo.load = PipelineCacheInfo::Load::Loaded; }
            }
        }
    }
    if (!linked) {
        glProgramParameteri(p.id, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
        glAttachShader(p.id, a->id); if (b) glAttachShader(p.id, b->id); glLinkProgram(p.id);
        glGetProgramiv(p.id, GL_LINK_STATUS, &linked);
    }
    if (!linked) {
        char log[8192]{}; glGetProgramInfoLog(p.id, sizeof(log), nullptr, log);
        AUREA_LOG_ERROR("GLES pipeline %s: %s", desc.debugName ? desc.debugName : "", log);
        glDeleteProgram(p.id); return Status{Errc::PipelineCompileFailed};
    }
    for (u32 stage = 0; stage < 3; ++stage) for (u32 slot = 0; slot < binding::kTextureSlots; ++slot) {
        const char* names[] = {"vs", "fs", "cs"};
        const auto name = std::string("aurea_border_") + names[stage] + std::to_string(slot);
        p.borderLocations[stage][slot] = glGetUniformLocation(p.id, name.c_str());
    }
    const u64 id = d.nextId++; d.pipelines.emplace(id, std::move(p)); return PipelineHandle{id};
}
void Backend::destroy_pipeline(PipelineHandle h) noexcept {
    auto& d = *impl_; Impl::Scope scope(d); auto it = d.pipelines.find(h.id);
    if (scope.valid && it != d.pipelines.end()) {
        if (d.pipeline == &it->second) d.pipeline = nullptr;
        glDeleteProgram(it->second.id); d.pipelines.erase(it);
    }
}
void Backend::save_pipeline_cache() noexcept {
    auto& d = *impl_; Impl::Scope scope(d); if (!scope.valid || d.cacheDirectory.empty()) return;
    std::error_code error; std::filesystem::create_directories(d.cacheDirectory, error);
    if (error) { AUREA_LOG_WARN("GLES pipeline cache directory: %s", error.message().c_str()); return; }
    for (auto& [id, p] : d.pipelines) {
        if (p.persisted) continue;
        GLint bytes = 0; glGetProgramiv(p.id, GL_PROGRAM_BINARY_LENGTH, &bytes); if (bytes <= 0) continue;
        std::vector<char> binary(bytes); GLenum format = 0; GLsizei written = 0;
        glGetProgramBinary(p.id, bytes, &written, &format, binary.data());
        if (!d.check("get program binary").ok() || written <= 0) continue;
        const auto path = d.cache_path(p.cacheKey); const auto temporary = path + ".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        const u32 header[2] = {0x314c4741u, format};
        file.write(reinterpret_cast<const char*>(header), sizeof(header)); file.write(binary.data(), written); file.close();
        if (!file || std::rename(temporary.c_str(), path.c_str()) != 0) AUREA_LOG_WARN("GLES pipeline cache write failed");
        else p.persisted = true;
    }
}

Status Backend::upload_texture_level(TextureHandle h, u32 mip, u32 layer, const void* data, usize bytes) noexcept {
    auto& d = *impl_; Impl::Scope scope(d); auto* t = find(d.textures, h.id);
    if (!scope.valid || !t) return Status{Errc::InvalidState};
    if (!data || mip >= std::max(1u, t->desc.mipLevels) || layer >= t->desc.layers) return Status{Errc::OutOfRange};
    const Format f = format_of(t->desc.format);
    const u32 w = std::max(1u, t->desc.width >> mip), ht = std::max(1u, t->desc.height >> mip);
    const u32 depth = t->target == GL_TEXTURE_3D ? std::max(1u, t->desc.depth >> mip) : 1;
    const usize needed = f.compressed ? static_cast<usize>((w + 3) / 4) * ((ht + 3) / 4) * 16 * depth
                                     : static_cast<usize>(w) * ht * depth * f.bytes;
    if (bytes < needed) return Status{Errc::OutOfRange};
    glBindTexture(t->target, t->id); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    const GLenum target = t->desc.cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer : t->target;
    if (t->target == GL_TEXTURE_3D || t->target == GL_TEXTURE_2D_ARRAY) {
        if (f.compressed) glCompressedTexSubImage3D(target, mip, 0, 0, layer, w, ht, depth, f.internal, needed, data);
        else glTexSubImage3D(target, mip, 0, 0, layer, w, ht, depth, f.external, f.type, data);
    } else if (f.compressed) glCompressedTexSubImage2D(target, mip, 0, 0, w, ht, f.internal, needed, data);
    else glTexSubImage2D(target, mip, 0, 0, w, ht, f.external, f.type, data);
    d.uploadBytes += needed; return d.check("upload_texture_level");
}
Status Backend::upload_texture(TextureHandle h, const void* data, u32 stride) noexcept {
    auto& d = *impl_; Impl::Scope scope(d); auto* t = find(d.textures, h.id);
    if (!scope.valid || !t) return Status{Errc::InvalidState};
    const auto f = format_of(t->desc.format);
    if (!data || f.compressed || t->target != GL_TEXTURE_2D) return Status{Errc::InvalidArgument};
    if (!stride) stride = t->desc.width * f.bytes;
    if (stride < t->desc.width * f.bytes || stride % f.bytes) return Status{Errc::OutOfRange};
    const usize rowBytes = static_cast<usize>(t->desc.width) * f.bytes;
    // Own the transfer memory. Passing an ImageReader's mapped plane directly
    // deadlocks the emulator GL transport; a packed CPU copy also avoids
    // exposing decoder padding/lifetime to the graphics driver.
    auto& staged = d.uploadScratch;
    staged.resize(rowBytes * t->desc.height);
    for (u32 y = 0; y < t->desc.height; ++y)
        std::memcpy(staged.data() + y * rowBytes, static_cast<const u8*>(data) + static_cast<usize>(y) * stride, rowBytes);
    glBindTexture(t->target, t->id); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexSubImage2D(t->target, 0, 0, 0, t->desc.width, t->desc.height, f.external, f.type, staged.data());
    d.uploadBytes += rowBytes * t->desc.height;
    // Retain common preview sizes, but don't pin the largest imported image.
    if (staged.capacity() > 16 * 1024 * 1024) std::vector<u8>().swap(staged);
    return d.check("upload_texture");
}
Status Backend::generate_mipmaps(TextureHandle h) noexcept {
    auto& d = *impl_; Impl::Scope scope(d); auto* t = find(d.textures, h.id);
    if (!scope.valid || !t) return Status{Errc::InvalidState};
    if (is_block_compressed(t->desc.format) || t->desc.is_depth()) return Status{Errc::NotSupported};
    glBindTexture(t->target, t->id); glGenerateMipmap(t->target); return d.check("generate_mipmaps");
}
Status Backend::read_texture(TextureHandle h, void* output, u32 stride) noexcept {
    auto& d = *impl_; Impl::Scope scope(d); auto* t = find(d.textures, h.id);
    if (!scope.valid || !t) return Status{Errc::InvalidState};
    const auto f = format_of(t->desc.format);
    if (!output || f.compressed || t->desc.is_depth() || t->target != GL_TEXTURE_2D) return Status{Errc::NotSupported};
    if (stride < t->desc.width * f.bytes) return Status{Errc::OutOfRange};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, d.readFramebuffer); d.attach(GL_READ_FRAMEBUFFER, t, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    const bool floating = f.type == GL_HALF_FLOAT || f.type == GL_FLOAT;
    const usize pixels = static_cast<usize>(t->desc.width) * t->desc.height;
    const u32 components = f.external == GL_RED ? 1 : f.external == GL_RG ? 2 : 4;
    if (floating) {
        std::vector<f32> data(pixels * 4); glReadPixels(0, 0, t->desc.width, t->desc.height, GL_RGBA, GL_FLOAT, data.data());
        for (u32 y = 0; y < t->desc.height; ++y) for (u32 x = 0; x < t->desc.width; ++x) for (u32 c = 0; c < components; ++c) {
            u8* dst = static_cast<u8*>(output) + static_cast<usize>(y) * stride + x * f.bytes;
            const f32 value = data[(static_cast<usize>(y) * t->desc.width + x) * 4 + c];
            if (f.type == GL_HALF_FLOAT) { const u16 half = scene3d::float_to_half(value); std::memcpy(dst + c * 2, &half, 2); }
            else std::memcpy(dst + c * 4, &value, 4);
        }
    } else if (f.type == GL_UNSIGNED_BYTE) {
        std::vector<u8> data(pixels * 4); glReadPixels(0, 0, t->desc.width, t->desc.height, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
        for (u32 y = 0; y < t->desc.height; ++y) for (u32 x = 0; x < t->desc.width; ++x)
            std::memcpy(static_cast<u8*>(output) + static_cast<usize>(y) * stride + x * components,
                        data.data() + (static_cast<usize>(y) * t->desc.width + x) * 4, components);
    } else return Status{Errc::NotSupported};
    return d.check("read_texture");
}
Result<ExternalTexture> Backend::import_external_image(const ExternalImageDesc&) noexcept {
    return Status{Errc::UnsupportedFeature, "GLES uses the platform CPU-plane video decoder"};
}
void Backend::release_external_image(TextureHandle h) noexcept { destroy_texture(h); }
} // namespace aurea::gles

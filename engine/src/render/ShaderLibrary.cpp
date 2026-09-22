#include "aurea/render/ShaderLibrary.hpp"
#include "aurea/core/Log.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace aurea {

usize PipelineKeyHash::operator()(const PipelineKey& k) const noexcept {
    // FNV-1a em 64 bits sobre os campos, reduzido no fim. Misturar em `usize`
    // direto truncaria em armeabi-v7a (32 bits) e faria chaves colidirem.
    u64 h = 1469598103934665603ull;
    auto mix = [&h](u64 v) { h ^= v; h *= 1099511628211ull; };
    mix(static_cast<u64>(k.vertex));
    mix(static_cast<u64>(k.fragment));
    mix(static_cast<u64>(k.compute));
    mix(static_cast<u64>(k.blendEnabled));
    mix(static_cast<u64>(k.blend));
    mix(static_cast<u64>(k.format));
    mix(static_cast<u64>(k.topology));
    mix(k.immutableSampler);
    mix(static_cast<u64>(k.mesh) | (static_cast<u64>(k.hasDepth) << 8) | (static_cast<u64>(k.depthOnly) << 9)
        | (static_cast<u64>(k.depthTest) << 10) | (static_cast<u64>(k.depthWrite) << 11)
        | (static_cast<u64>(k.depthCompare) << 12) | (static_cast<u64>(k.depthFormat) << 16)
        | (static_cast<u64>(k.cull) << 32) | (static_cast<u64>(k.frontFaceCCW) << 36));
    u32 bc = 0, bs = 0;
    std::memcpy(&bc, &k.depthBiasConstant, 4);
    std::memcpy(&bs, &k.depthBiasSlope, 4);
    mix((static_cast<u64>(bc) << 32) | bs);
    return static_cast<usize>(h ^ (h >> 32));
}

VertexLayout vertex_layout_for(MeshLayout layout) noexcept {
    VertexLayout v;
    if (layout == MeshLayout::None) return v;
    v.set_binding(0, kMeshPositionStride);
    v.add(0, 0, VertexFormat::Float3, 0);
    if (layout == MeshLayout::Static || layout == MeshLayout::Skinned) {
        v.set_binding(1, kMeshShadingStride);
        v.add(1, 1, VertexFormat::Float3, 0);        // normal
        v.add(2, 1, VertexFormat::Float4, 12);       // tangente (w = sinal)
        v.add(3, 1, VertexFormat::Float2, 28);       // uv0
        v.add(4, 1, VertexFormat::Float2, 36);       // uv1
        v.add(5, 1, VertexFormat::UByte4Norm, 44);   // cor
    }
    if (layout == MeshLayout::Skinned || layout == MeshLayout::SkinnedPosition) {
        v.set_binding(2, kMeshSkinStride);
        v.add(6, 2, VertexFormat::UShort4, 0);       // juntas
        v.add(7, 2, VertexFormat::UShort4Norm, 8);   // pesos
    }
    return v;
}

namespace {

SamplerDesc common_sampler_desc(CommonSampler s) noexcept {
    SamplerDesc d;
    switch (s) {
        case CommonSampler::LinearClamp: break;
        case CommonSampler::LinearBorder:
            d.wrapU = d.wrapV = SamplerDesc::Wrap::ClampToBorder;
            break;
        case CommonSampler::NearestClamp:
            d.minFilter = d.magFilter = SamplerDesc::Filter::Nearest;
            break;
        case CommonSampler::LinearRepeat:
            d.wrapU = d.wrapV = SamplerDesc::Wrap::Repeat;
            break;
        case CommonSampler::LinearMirror:
            d.wrapU = d.wrapV = SamplerDesc::Wrap::MirroredRepeat;
            break;
        case CommonSampler::Count: break;
    }
    return d;
}

} // namespace

Status ShaderLibrary::initialize(GPUBackend& backend) noexcept {
    backend_ = &backend;
    overrides_.assign(kShaderCount, {});
    overrideStamp_.assign(kShaderCount, 0);

    for (u32 i = 0; i < kShaderCount; ++i) {
        const ShaderBlob& blob = shader_blob(static_cast<ShaderId>(i));
        ShaderDesc desc;
        desc.stage = kShaderStages[i];
        desc.spirv = blob.words;
        desc.spirvBytes = blob.bytes;
        desc.debugName = kShaderNames[i];
        auto created = backend.create_shader(desc);
        if (!created.ok()) {
            lastError_ = std::string("shader nao criado: ") + kShaderNames[i];
            AUREA_LOG_ERROR("%s", lastError_.c_str());
            ++failures_;
            return created.status();
        }
        shaders_[i] = *created;
    }

    for (u32 i = 0; i < static_cast<u32>(CommonSampler::Count); ++i) {
        auto created = backend.create_sampler(common_sampler_desc(static_cast<CommonSampler>(i)));
        if (!created.ok()) return created.status();
        samplers_[i] = *created;
    }
    return OkStatus;
}

void ShaderLibrary::shutdown() noexcept {
    if (!backend_) return;
    for (auto& [key, handle] : pipelines_) backend_->destroy_pipeline(handle);
    for (ShaderHandle& s : shaders_) {
        if (s.valid()) backend_->destroy_shader(s);
        s = ShaderHandle{};
    }
    for (SamplerHandle& s : samplers_) {
        if (s.valid()) backend_->destroy_sampler(s);
        s = SamplerHandle{};
    }
    forget_device();
}

void ShaderLibrary::forget_device() noexcept {
    pipelines_.clear();
    for (ShaderHandle& s : shaders_) s = ShaderHandle{};
    for (SamplerHandle& s : samplers_) s = SamplerHandle{};
    backend_ = nullptr;
}

ShaderHandle ShaderLibrary::shader(ShaderId id) const noexcept {
    const u32 i = static_cast<u32>(id);
    return i < kShaderCount ? shaders_[i] : ShaderHandle{};
}

SamplerHandle ShaderLibrary::sampler(CommonSampler s) const noexcept {
    const u32 i = static_cast<u32>(s);
    return i < static_cast<u32>(CommonSampler::Count) ? samplers_[i] : SamplerHandle{};
}

Result<PipelineHandle> ShaderLibrary::pipeline(const PipelineKey& key) noexcept {
    if (!backend_) return Status{Errc::InvalidState, "biblioteca de shaders sem backend"};
    if (const auto it = pipelines_.find(key); it != pipelines_.end()) return it->second;

    PipelineDesc desc;
    desc.isCompute = key.is_compute();
    if (desc.isCompute) {
        desc.computeShader = shader(key.compute);
        desc.debugName = kShaderNames[static_cast<u32>(key.compute)];
    } else {
        desc.vertexShader = shader(key.vertex);
        desc.fragmentShader = shader(key.fragment);
        desc.debugName = key.fragment != ShaderId::Count
                       ? kShaderNames[static_cast<u32>(key.fragment)] : "pipeline";
    }
    desc.blendEnabled = key.blendEnabled;
    desc.blend = key.blend;
    desc.colorFormat = key.format;
    desc.topology = key.topology;
    desc.immutableSampler0 = SamplerHandle{key.immutableSampler};
    desc.vertexLayout = vertex_layout_for(key.mesh);
    desc.hasDepth = key.hasDepth;
    desc.depthOnly = key.depthOnly;
    desc.depth.test = key.depthTest;
    desc.depth.write = key.depthWrite;
    desc.depth.compare = key.depthCompare;
    desc.depth.biasConstant = key.depthBiasConstant;
    desc.depth.biasSlope = key.depthBiasSlope;
    desc.depthFormat = key.depthFormat;
    desc.cull = key.cull;
    desc.frontFaceCCW = key.frontFaceCCW;

    auto created = backend_->create_pipeline(desc);
    if (!created.ok()) {
        ++failures_;
        lastError_ = std::string("pipeline nao compilou: ") + (desc.debugName ? desc.debugName : "?");
        AUREA_LOG_ERROR("%s", lastError_.c_str());
        return created.status();
    }
    if (steady_) {
        ++compilesSinceMark_;
        AUREA_LOG_WARN("pipeline '%s' criado durante o playback", desc.debugName ? desc.debugName : "?");
    }
    pipelines_.emplace(key, *created);
    return *created;
}

u32 ShaderLibrary::prewarm(const PipelineKey* keys, u32 count) noexcept {
    u32 created = 0;
    const bool wasSteady = steady_;
    steady_ = false;   // pré-aquecer não é "durante o playback"
    for (u32 i = 0; i < count; ++i) {
        const usize before = pipelines_.size();
        (void)pipeline(keys[i]);
        if (pipelines_.size() > before) ++created;
    }
    steady_ = wasSteady;
    return created;
}

u32 ShaderLibrary::reload_changed(const char* spvDirectory) noexcept {
    if (!backend_ || !spvDirectory || !*spvDirectory) return 0;
    namespace fs = std::filesystem;
    u32 reloaded = 0;

    for (u32 i = 0; i < kShaderCount; ++i) {
        std::error_code ec;
        const fs::path path = fs::path(spvDirectory) / (std::string(kShaderNames[i]) + ".spv");
        const auto stamp = fs::last_write_time(path, ec);
        if (ec) continue;
        const i64 ticks = static_cast<i64>(stamp.time_since_epoch().count());
        if (overrideStamp_[i] == 0) { overrideStamp_[i] = ticks; continue; }   // primeira visita
        if (ticks == overrideStamp_[i]) continue;

        std::FILE* f = std::fopen(path.string().c_str(), "rb");
        if (!f) continue;
        std::vector<u32> words;
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (size > 0 && size % 4 == 0) {
            words.resize(static_cast<usize>(size) / 4);
            if (std::fread(words.data(), 1, static_cast<usize>(size), f) != static_cast<usize>(size)) {
                words.clear();
            }
        }
        std::fclose(f);
        if (words.empty()) continue;

        ShaderDesc desc;
        desc.stage = kShaderStages[i];
        desc.spirv = words.data();
        desc.spirvBytes = words.size() * 4;
        desc.debugName = kShaderNames[i];
        auto created = backend_->create_shader(desc);
        if (!created.ok()) {
            AUREA_LOG_WARN("recarga: '%s' nao compilou, mantendo a versao anterior", kShaderNames[i]);
            overrideStamp_[i] = ticks;
            continue;
        }
        backend_->destroy_shader(shaders_[i]);
        shaders_[i] = *created;
        overrides_[i] = std::move(words);
        overrideStamp_[i] = ticks;

        // Todo pipeline que usava o shader antigo é descartado; o próximo
        // frame o recria com o novo.
        const ShaderId id = static_cast<ShaderId>(i);
        for (auto it = pipelines_.begin(); it != pipelines_.end();) {
            const PipelineKey& k = it->first;
            if (k.vertex == id || k.fragment == id || k.compute == id) {
                backend_->destroy_pipeline(it->second);
                it = pipelines_.erase(it);
            } else {
                ++it;
            }
        }
        ++reloaded;
        AUREA_LOG_INFO("shader recarregado: %s", kShaderNames[i]);
    }
    return reloaded;
}

} // namespace aurea

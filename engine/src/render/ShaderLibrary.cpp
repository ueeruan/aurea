#include "aurea/render/ShaderLibrary.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>
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
    mix(static_cast<u64>(k.sampleCount) | (static_cast<u64>(k.alphaToCoverage) << 8)
        | (static_cast<u64>(k.hasColor1) << 9));
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
        case CommonSampler::ShadowCompare:
            d.compare = true;   // linear + clamp: PCF 2×2 do hardware por amostra
            break;
        case CommonSampler::Count: break;
    }
    return d;
}

} // namespace

u64 ShaderLibrary::spirv_fingerprint() noexcept {
    static const u64 fp = [] {
        u64 h = 1469598103934665603ull;
        auto mix = [&h](u64 v) { h ^= v; h *= 1099511628211ull; };
        mix(kShaderCount);
        for (u32 i = 0; i < kShaderCount; ++i) {
            const ShaderBlob& blob = shader_blob(static_cast<ShaderId>(i));
            mix(blob.bytes);
            const usize words = blob.bytes / 4;
            for (usize w = 0; w < words; ++w) mix(blob.words[w]);
        }
        return h ? h : 1ull;   // 0 é "não confere"
    }();
    return fp;
}

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
            // Um shader recusado (driver GLES sem suporte a algo de um efeito
            // novo) não derruba o motor inteiro: sem ele, só os passes que o
            // pedem ficam sem pipeline e são pulados (bypass do efeito).
            lastError_ = std::string("shader nao criado: ") + kShaderNames[i];
            AUREA_LOG_ERROR("%s", lastError_.c_str());
            ++failures_;
            ++missingShaders_;
            shaders_[i] = ShaderHandle{};
            continue;
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
    failedKeys_.clear();
    missingShaders_ = 0;
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
    if (testFailing_ != ShaderId::Count
        && (key.vertex == testFailing_ || key.fragment == testFailing_ || key.compute == testFailing_)) {
        return Status{Errc::PipelineCompileFailed, "teste: pipeline recusado"};
    }
    if (const auto it = pipelines_.find(key); it != pipelines_.end()) return it->second;
    // Falhou há pouco: não recompila (nem loga) a cada quadro — um link
    // recusado no GLES custa dezenas de ms. Tenta de novo depois de
    // kRetryAfter pedidos (falta de memória passa; recusa do driver volta).
    if (const auto it = failedKeys_.find(key); it != failedKeys_.end()) {
        if (--it->second > 0) return Status{Errc::PipelineCompileFailed};
        failedKeys_.erase(it);
    }
    {
        // Shader que o backend recusou na inicialização: o pipeline não existe.
        const bool missing = key.is_compute()
            ? !shader(key.compute).valid()
            : (!shader(key.vertex).valid() || (key.fragment != ShaderId::Count && !shader(key.fragment).valid()));
        if (missing) {
            ++failures_;
            const ShaderId named = key.is_compute() ? key.compute
                                 : key.fragment != ShaderId::Count ? key.fragment : key.vertex;
            lastError_ = std::string("pipeline sem shader: ")
                       + (static_cast<u32>(named) < kShaderCount ? kShaderNames[static_cast<u32>(named)] : "?");
            AUREA_LOG_ERROR("%s", lastError_.c_str());
            failedKeys_[key] = kRetryAfter;
            return Status{Errc::ShaderCompileFailed};
        }
    }

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
    desc.sampleCount = std::max<u32>(1u, key.sampleCount);
    desc.alphaToCoverage = key.alphaToCoverage && desc.sampleCount > 1;
    desc.hasColor1 = key.hasColor1 && !key.depthOnly;
    desc.colorFormat1 = key.format;

    auto created = backend_->create_pipeline(desc);
    if (!created.ok()) {
        ++failures_;
        lastError_ = std::string("pipeline nao compilou: ") + (desc.debugName ? desc.debugName : "?");
        AUREA_LOG_ERROR("%s", lastError_.c_str());
        failedKeys_[key] = kRetryAfter;
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
        failedKeys_.clear();   // o shader novo pode compilar o que falhou
        AUREA_LOG_INFO("shader recarregado: %s", kShaderNames[i]);
    }
    return reloaded;
}

} // namespace aurea

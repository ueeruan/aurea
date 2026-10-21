#include "aurea/render/ShaderLibrary.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/render/RuntimeShaders.hpp"
#include "aurea/core/Time.hpp"

#include <cmath>
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
        case CommonSampler::LinearAnisotropicClamp:
            d.maxAnisotropy = 16.0f; // backends clamp to their actual capability
            break;
        case CommonSampler::Count: break;
    }
    return d;
}

} // namespace

f32 ShaderLibrary::max_sampler_anisotropy() const noexcept {
    if (!backend_) return 1.0f;
    const f32 supported = backend_->capabilities().maxSamplerAnisotropy;
    return std::isfinite(supported) ? std::clamp(supported, 1.0f, 16.0f) : 1.0f;
}

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
    deferredShaders_ = backend.defer_shader_creation();
    overrides_.assign(kShaderCount, {});
    overrideStamp_.assign(kShaderCount, 0);

    if (!deferredShaders_)
        for (u32 i = 0; i < kShaderCount; ++i) (void)ensure_shader(static_cast<ShaderId>(i));

    for (u32 i = 0; i < static_cast<u32>(CommonSampler::Count); ++i) {
        auto created = backend.create_sampler(common_sampler_desc(static_cast<CommonSampler>(i)));
        if (!created.ok()) return created.status();
        samplers_[i] = *created;
    }
    return OkStatus;
}

const char* ShaderLibrary::shader_name(ShaderId id, std::string& storage) const {
    const u32 i = static_cast<u32>(id);
    if (i < kShaderCount) return kShaderNames[i];
    if (is_runtime_shader(id)) {
        if (const auto source = runtime_shaders::find(id)) storage = source->name;
        else storage = "shader de plugin removido";
        return storage.c_str();
    }
    return "?";
}

std::string ShaderLibrary::runtime_shader_error(ShaderId id) const {
    const auto it = runtime_.find(static_cast<u16>(id));
    return it == runtime_.end() ? std::string{} : it->second.error;
}

bool ShaderLibrary::ensure_runtime_shader(ShaderId id) noexcept {
    if (!backend_) return false;
    RuntimeShader& entry = runtime_[static_cast<u16>(id)];
    if (entry.handle.valid()) return true;
    if (entry.retryRequests && --entry.retryRequests) return false;
    const auto source = runtime_shaders::find(id);
    if (!source || source->spirv.empty()) {
        entry.error = "shader de plugin nao registrado";
        entry.retryRequests = kRetryAfter;
        return false;
    }
    ShaderDesc desc;
    desc.stage = source->stage;
    desc.spirv = source->spirv.data();
    desc.spirvBytes = source->spirv.size() * sizeof(u32);
    desc.debugName = source->name.c_str();
    std::string error;
    auto created = backend_->create_runtime_shader(desc, &error);
    if (!created.ok()) {
        entry.error = error.empty() ? std::string(to_string(created.code())) : error;
        lastError_ = "shader de plugin nao criado: " + source->name + ": " + entry.error;
        AUREA_LOG_ERROR("%s", lastError_.c_str());
        ++failures_;
        entry.retryRequests = kRetryAfter;
        return false;
    }
    entry.handle = *created;
    entry.error.clear();
    entry.retryRequests = 0;
    return true;
}

void ShaderLibrary::sweep_runtime_shaders() noexcept {
    runtimeGeneration_ = runtime_shaders::generation();
    for (auto it = runtime_.begin(); it != runtime_.end();) {
        const ShaderId id = static_cast<ShaderId>(it->first);
        if (runtime_shaders::find(id)) { ++it; continue; }
        // O plugin saiu (ou foi reinstalado com outros ids): todo pipeline com
        // este shader sai junto. A destruição no backend é adiada até a GPU
        // terminar os quadros que podem tê-lo usado (GPUBackend::destroy_*).
        for (auto p = pipelines_.begin(); p != pipelines_.end();) {
            const PipelineKey& k = p->first;
            if (k.vertex == id || k.fragment == id || k.compute == id) {
                if (backend_) backend_->destroy_pipeline(p->second);
                p = pipelines_.erase(p);
            } else {
                ++p;
            }
        }
        for (auto f = failedKeys_.begin(); f != failedKeys_.end();) {
            const PipelineKey& k = f->first;
            if (k.vertex == id || k.fragment == id || k.compute == id) f = failedKeys_.erase(f);
            else ++f;
        }
        if (backend_ && it->second.handle.valid()) backend_->destroy_shader(it->second.handle);
        it = runtime_.erase(it);
    }
}

bool ShaderLibrary::ensure_shader(ShaderId id) noexcept {
    if (is_runtime_shader(id)) return ensure_runtime_shader(id);
    const u32 i = static_cast<u32>(id);
    if (!backend_ || i >= kShaderCount) return false;
    if (shaders_[i].valid()) return true;
    // A rejected effect must not re-enter the driver compiler every frame or
    // for every pipeline variant. Allow recovery after a bounded request gap.
    if (shaderRetryRequests_[i] && --shaderRetryRequests_[i]) return false;
    const ShaderBlob& blob = shader_blob(id);
    ShaderDesc desc;
    desc.stage = kShaderStages[i];
    desc.spirv = overrides_[i].empty() ? blob.words : overrides_[i].data();
    desc.spirvBytes = overrides_[i].empty() ? blob.bytes : overrides_[i].size() * sizeof(u32);
    desc.debugName = kShaderNames[i];
    auto created = backend_->create_shader(desc);
    if (!created.ok()) {
        lastError_ = std::string("shader nao criado: ") + kShaderNames[i];
        AUREA_LOG_ERROR("%s", lastError_.c_str());
        ++failures_;
        if (!rejectedShaders_[i]) { rejectedShaders_[i] = true; ++missingShaders_; }
        shaderRetryRequests_[i] = kRetryAfter;
        return false;
    }
    shaders_[i] = *created;
    shaderRetryRequests_[i] = 0;
    if (rejectedShaders_[i]) { rejectedShaders_[i] = false; --missingShaders_; }
    return true;
}

void ShaderLibrary::shutdown() noexcept {
    if (!backend_) return;
    for (auto& [key, handle] : pipelines_) backend_->destroy_pipeline(handle);
    for (ShaderHandle& s : shaders_) {
        if (s.valid()) backend_->destroy_shader(s);
        s = ShaderHandle{};
    }
    for (auto& [id, entry] : runtime_) {
        if (entry.handle.valid()) backend_->destroy_shader(entry.handle);
    }
    for (SamplerHandle& s : samplers_) {
        if (s.valid()) backend_->destroy_sampler(s);
        s = SamplerHandle{};
    }
    forget_device();
}

void ShaderLibrary::forget_device() noexcept {
    pipelineProbe_ = nullptr;
    pipelineProbeContext_ = nullptr;
    pipelines_.clear();
    failedKeys_.clear();
    missingShaders_ = 0;
    deferredShaders_ = false;
    std::fill(std::begin(shaderRetryRequests_), std::end(shaderRetryRequests_), 0u);
    std::fill(std::begin(rejectedShaders_), std::end(rejectedShaders_), false);
    for (ShaderHandle& s : shaders_) s = ShaderHandle{};
    for (SamplerHandle& s : samplers_) s = SamplerHandle{};
    runtime_.clear();
    runtimeGeneration_ = 0;
    backend_ = nullptr;
}

ShaderHandle ShaderLibrary::shader(ShaderId id) const noexcept {
    const u32 i = static_cast<u32>(id);
    if (i < kShaderCount) return shaders_[i];
    if (is_runtime_shader(id)) {
        const auto it = runtime_.find(static_cast<u16>(id));
        return it == runtime_.end() ? ShaderHandle{} : it->second.handle;
    }
    return ShaderHandle{};
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
    const bool runtimeKey = is_runtime_shader(key.vertex) || is_runtime_shader(key.fragment)
                         || is_runtime_shader(key.compute);
    // Um plugin entrou ou saiu do registro: solta o que dele ficou aqui.
    if (!runtime_.empty() && runtimeGeneration_ != runtime_shaders::generation()) sweep_runtime_shaders();
    if (const auto it = pipelines_.find(key); it != pipelines_.end()) return it->second;
    if (deferredShaders_ || runtimeKey) {
        // Requested shaders only: unrelated effects never enter a GLES driver
        // compiler during renderer startup. prewarm uses this same path.
        if (key.is_compute()) {
            if (!ensure_shader(key.compute)) return Status{Errc::ShaderCompileFailed};
        } else {
            const bool vertex = ensure_shader(key.vertex);
            const bool fragment = key.fragment == ShaderId::Count || ensure_shader(key.fragment);
            if (!vertex || !fragment) return Status{Errc::ShaderCompileFailed};
        }
    }
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
            std::string storage;
            lastError_ = std::string("pipeline sem shader: ") + shader_name(named, storage);
            AUREA_LOG_ERROR("%s", lastError_.c_str());
            failedKeys_[key] = kRetryAfter;
            return Status{Errc::ShaderCompileFailed};
        }
    }

    PipelineDesc desc;
    std::string nameStorage;
    desc.isCompute = key.is_compute();
    if (desc.isCompute) {
        desc.computeShader = shader(key.compute);
        desc.debugName = shader_name(key.compute, nameStorage);
    } else {
        desc.vertexShader = shader(key.vertex);
        desc.fragmentShader = shader(key.fragment);
        desc.debugName = key.fragment != ShaderId::Count ? shader_name(key.fragment, nameStorage) : "pipeline";
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

    const u64 createStarted = pipelineProbe_ ? monotonic_ns() : 0;
    auto created = backend_->create_pipeline(desc);
    if (pipelineProbe_)
        pipelineProbe_(pipelineProbeContext_, key, monotonic_ns() - createStarted, created.ok());
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
        if (shaders_[i].valid()) backend_->destroy_shader(shaders_[i]);
        shaders_[i] = *created;
        shaderRetryRequests_[i] = 0;
        if (rejectedShaders_[i]) { rejectedShaders_[i] = false; --missingShaders_; }
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

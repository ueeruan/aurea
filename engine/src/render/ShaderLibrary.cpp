#include "aurea/render/ShaderLibrary.hpp"
#include "aurea/core/Log.hpp"
#include "aurea/shaders/Composite.h"

#include <cstring>

namespace aurea {

bool operator==(const PipelineKey& a, const PipelineKey& b) noexcept {
    if (a.isCompute != b.isCompute) return false;
    if (a.isCompute) return a.compute == b.compute;
    if (!(a.vertex == b.vertex)) return false;
    if (!(a.fragment == b.fragment)) return false;
    if (a.blend != b.blend) return false;
    if (a.depthTest != b.depthTest || a.depthWrite != b.depthWrite) return false;
    if (a.cullBackFace != b.cullBackFace) return false;
    if (a.colorAttachmentCount != b.colorAttachmentCount) return false;
    if (a.depthFormat != b.depthFormat) return false;
    if (a.sampleCount != b.sampleCount) return false;
    for (u32 i = 0; i < 4; ++i) {
        if (a.colorFormats[i] != b.colorFormats[i]) return false;
    }
    return true;
}

usize PipelineKeyHash::operator()(const PipelineKey& k) const noexcept {
    if (k.isCompute) {
        return ShaderKeyHash{}(k.compute) ^ 0x5DEECE66Dull;
    }
    usize h = ShaderKeyHash{}(k.vertex);
    h ^= ShaderKeyHash{}(k.fragment) * 0x9E3779B97F4A7C15ull;
    h ^= static_cast<usize>(k.blend) * 0xC2B2AE3D27D4EB4Full;
    h ^= static_cast<usize>(k.depthTest) << 3;
    h ^= static_cast<usize>(k.depthWrite) << 4;
    h ^= static_cast<usize>(k.cullBackFace) << 5;
    h ^= static_cast<usize>(k.colorAttachmentCount) << 6;
    h ^= static_cast<usize>(k.depthFormat) << 9;
    h ^= static_cast<usize>(k.sampleCount) << 13;
    for (u32 i = 0; i < 4; ++i) {
        h ^= static_cast<usize>(k.colorFormats[i]) << (16 + i * 4);
    }
    return h;
}

Status ShaderLibrary::initialize(GPUBackend& backend) noexcept {
    backend_ = &backend;
    shaders_.reserve(64);
    pipelines_.reserve(128);
    shaderIndex_.reserve(64);
    pipelineIndex_.reserve(128);
    return OkStatus;
}

void ShaderLibrary::shutdown() noexcept {
    shaders_.clear();
    pipelines_.clear();
    shaderIndex_.clear();
    pipelineIndex_.clear();
    backend_ = nullptr;
}

Result<ShaderHandle> ShaderLibrary::get_shader(const ShaderKey& key) noexcept {
    if (!backend_) {
        return Status{Errc::InvalidState, "biblioteca de shaders nao inicializada"};
    }
    if (!key.source) {
        return Status{Errc::InvalidArgument, "shader sem fonte"};
    }

    auto it = shaderIndex_.find(key);
    if (it != shaderIndex_.end()) {
        ShaderEntry& entry = shaders_[it->second];
        if (entry.valid) {
            ++hits_;
            return entry.handle;
        }
    }

    ++misses_;
    ShaderDesc desc;
    desc.stage = key.stage;
    desc.source = key.source;
    desc.entryPoint = "main";
    desc.debugName = key.source;

    auto res = backend_->create_shader(desc);
    if (!res.ok()) {
        ++compileFailures_;
        lastError_ = "falha ao compilar shader (variante ";
        lastError_ += std::to_string(key.variantFlags);
        lastError_ += ")";
        AUREA_LOG_ERROR("%s", lastError_.c_str());
        return res.status();
    }

    if (it != shaderIndex_.end()) {
        shaders_[it->second].handle = *res;
        shaders_[it->second].valid = true;
        return *res;
    }

    ShaderEntry entry;
    entry.key = key;
    entry.handle = *res;
    entry.valid = true;
    shaders_.push_back(entry);
    shaderIndex_.emplace(key, static_cast<u32>(shaders_.size()) - 1);
    return entry.handle;
}

void ShaderLibrary::invalidate_device_shaders() noexcept {
    // O dispositivo foi recriado: os handles do driver morreram. As ENTRADAS do
    // cache continuam — a chave estrutural não mudou, então na próxima
    // requisição o shader é recompilado e o handle do cache é atualizado.
    for (auto& entry : shaders_) {
        entry.handle = ShaderHandle{};
        entry.valid = false;
    }
    for (auto& entry : pipelines_) {
        entry.handle = PipelineHandle{};
        entry.valid = false;
    }
}

Result<PipelineHandle> ShaderLibrary::get_pipeline(const PipelineKey& key) noexcept {
    if (!backend_) {
        return Status{Errc::InvalidState, "biblioteca de shaders nao inicializada"};
    }

    auto it = pipelineIndex_.find(key);
    if (it != pipelineIndex_.end()) {
        PipelineEntry& entry = pipelines_[it->second];
        if (entry.valid) {
            ++hits_;
            return entry.handle;
        }
    }

    ++misses_;

    PipelineDesc desc;

    if (key.isCompute) {
        auto cs = get_shader(key.compute);
        if (!cs.ok()) return cs.status();

        desc.isCompute = true;
        desc.computeShader = *cs;

        auto res = backend_->create_pipeline(desc);
        if (!res.ok()) {
            ++compileFailures_;
            lastError_ = "falha ao compilar pipeline de compute";
            AUREA_LOG_ERROR("%s", lastError_.c_str());
            return res.status();
        }

        if (it != pipelineIndex_.end()) {
            pipelines_[it->second].handle = *res;
            pipelines_[it->second].valid = true;
            return *res;
        }
        PipelineEntry entry;
        entry.key = key;
        entry.handle = *res;
        entry.valid = true;
        pipelines_.push_back(entry);
        pipelineIndex_.emplace(key, static_cast<u32>(pipelines_.size()) - 1);
        return entry.handle;
    }

    // Os shaders do pipeline precisam existir antes dele.
    auto vs = get_shader(key.vertex);
    if (!vs.ok()) return vs.status();
    auto fs = get_shader(key.fragment);
    if (!fs.ok()) return fs.status();

    desc.vertexShader = *vs;
    desc.fragmentShader = *fs;
    desc.blend = key.blend;
    desc.depthTest = key.depthTest;
    desc.depthWrite = key.depthWrite;
    desc.cullBackFace = key.cullBackFace;
    desc.colorAttachmentCount = key.colorAttachmentCount;
    for (u32 i = 0; i < 4; ++i) desc.colorFormats[i] = key.colorFormats[i];
    desc.depthFormat = key.depthFormat;
    desc.sampleCount = key.sampleCount;

    auto res = backend_->create_pipeline(desc);
    if (!res.ok()) {
        ++compileFailures_;
        lastError_ = "falha ao compilar pipeline (blend ";
        lastError_ += std::to_string(static_cast<int>(key.blend));
        lastError_ += ")";
        AUREA_LOG_ERROR("%s", lastError_.c_str());
        return res.status();
    }

    if (it != pipelineIndex_.end()) {
        pipelines_[it->second].handle = *res;
        pipelines_[it->second].valid = true;
        return *res;
    }

    PipelineEntry entry;
    entry.key = key;
    entry.handle = *res;
    entry.valid = true;
    pipelines_.push_back(entry);
    pipelineIndex_.emplace(key, static_cast<u32>(pipelines_.size()) - 1);
    return entry.handle;
}

Result<PipelineHandle> ShaderLibrary::composite_pipeline(BlendMode blend,
                                                         SurfaceFormat targetFormat,
                                                         u32 sampleCount) noexcept {
    PipelineKey key;
    // A fonte do shader de composição é fixa: um quad texturizado com o blend
    // selecionado. Preview e export usam exatamente esta — é por isso que eles
    // não podem divergir visualmente.
    key.vertex.source   = kCompositeVertexSource;
    key.vertex.stage    = ShaderStage::Vertex;
    key.fragment.source = kCompositeFragmentSource;
    key.fragment.stage  = ShaderStage::Fragment;
    key.blend = blend;
    key.depthTest = false;
    key.depthWrite = false;
    key.cullBackFace = false;
    key.colorAttachmentCount = 1;
    key.colorFormats[0] = targetFormat;
    key.sampleCount = sampleCount;
    return get_pipeline(key);
}

Result<PipelineHandle> ShaderLibrary::compute_pipeline(const ShaderKey& shader) noexcept {
    PipelineKey key;
    key.isCompute = true;
    key.compute = shader;
    // Sem estágios gráficos, sem anexos de cor, sem blend: um pipeline de
    // compute é declarado como compute e o backend monta o estado certo.
    key.colorAttachmentCount = 0;
    return get_pipeline(key);
}

u32 ShaderLibrary::prewarm_blend_modes(const BlendMode* modes, u32 count,
                                       SurfaceFormat targetFormat,
                                       u32 sampleCount) noexcept {
    if (!modes) return 0;
    u32 compiled = 0;
    for (u32 i = 0; i < count; ++i) {
        PipelineKey key;
        key.vertex.source   = kCompositeVertexSource;
        key.vertex.stage    = ShaderStage::Vertex;
        key.fragment.source = kCompositeFragmentSource;
        key.fragment.stage  = ShaderStage::Fragment;
        key.blend = modes[i];
        key.colorFormats[0] = targetFormat;
        key.sampleCount = sampleCount;

        const bool alreadyCached = pipelineIndex_.find(key) != pipelineIndex_.end();
        auto res = get_pipeline(key);
        if (res.ok() && !alreadyCached) ++compiled;
    }
    // Compilar 12 blends antes do primeiro frame custa ~100 ms uma vez; deixar
    // para a primeira vez que cada um aparece custa 12 engasgos durante o
    // playback. O primeiro é claramente melhor.
    AUREA_LOG_INFO("ShaderLibrary: pre-aquecidos %u pipelines de blend", compiled);
    return compiled;
}

u32 ShaderLibrary::prewarm_effects(const u32* effectTypeIds, u32 count,
                                   SurfaceFormat targetFormat) noexcept {
    if (!effectTypeIds || count == 0) return 0;
    // Cada efeito tem um ou dois pipelines (um por classe de fusão). Pré-aquecer
    // aqui garante que o EXPORT nunca compile nada no meio — um compile de 200 ms
    // no meio da exportação seria 200 ms de vídeo com o frame errado, ou uma
    // pausa de uma eternidade.
    u32 compiled = 0;
    for (u32 i = 0; i < count; ++i) {
        PipelineKey key;
        key.vertex.source   = kCompositeVertexSource;
        key.vertex.stage    = ShaderStage::Vertex;
        key.fragment.source = kCompositeFragmentSource;
        key.fragment.stage  = ShaderStage::Fragment;
        key.fragment.variantFlags = effectTypeIds[i];
        key.blend = BlendMode::Normal;
        key.colorFormats[0] = targetFormat;

        const bool alreadyCached = pipelineIndex_.find(key) != pipelineIndex_.end();
        auto res = get_pipeline(key);
        if (res.ok() && !alreadyCached) ++compiled;
    }
    return compiled;
}

} // namespace aurea

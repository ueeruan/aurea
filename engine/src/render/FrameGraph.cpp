#include "aurea/render/FrameGraph.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>
#include <sstream>

namespace aurea {

FrameGraph::FrameGraph(usize /*arenaBytes*/) {
    passes_.reserve(64);
    resources_.reserve(64);
    physical_.reserve(32);
    order_.reserve(64);
    marks_.reserve(64);
}

void FrameGraph::reset() noexcept {
    passes_.clear();
    resources_.clear();
    order_.clear();
    outputs_.clear();
    marks_.clear();
    passCount_ = 0;
    resourceCount_ = 0;
    culledCount_ = 0;
    mergedCount_ = 0;
    compiled_ = false;

    // `physical_` NÃO é limpo: as texturas do frame anterior são reaproveitadas.
    // Realocar 4K a 60 Hz seria o gargalo dominante do editor.
    for (auto& p : physical_) p.busyUntilPass = kInvalidIndex;
    resourceToPhysical_.clear();
    ++revision_;
}

u32 FrameGraph::create_texture(std::string name, const TextureDesc& desc) noexcept {
    if (resourceCount_ >= kMaxResources) {
        AUREA_LOG_ERROR("FrameGraph: limite de %u recursos", kMaxResources);
        return kInvalidIndex;
    }
    FrameResource r;
    r.name = std::move(name);
    r.desc = desc;
    r.firstPass = kInvalidIndex;
    r.lastPass = 0;
    resources_.push_back(std::move(r));
    ++revision_;
    return resourceCount_++;
}

u32 FrameGraph::create_buffer(std::string name, usize bytes, u32 usage) noexcept {
    if (resourceCount_ >= kMaxResources) return kInvalidIndex;
    FrameResource r;
    r.name = std::move(name);
    r.isBuffer = true;
    r.desc.width = static_cast<u32>(bytes);
    r.desc.usageHint = usage;
    r.firstPass = kInvalidIndex;
    r.lastPass = 0;
    resources_.push_back(std::move(r));
    ++revision_;
    return resourceCount_++;
}

u32 FrameGraph::import_texture(std::string name, TextureHandle external) noexcept {
    if (resourceCount_ >= kMaxResources) return kInvalidIndex;
    FrameResource r;
    r.name = std::move(name);
    r.external = true;
    r.physical = external;
    r.firstPass = kInvalidIndex;
    r.lastPass = 0;
    resources_.push_back(std::move(r));
    ++revision_;
    return resourceCount_++;
}

u32 FrameGraph::import_external_image(std::string name, const ExternalImageHandle& img,
                                      const TextureDesc& desc) noexcept {
    if (resourceCount_ >= kMaxResources) return kInvalidIndex;
    FrameResource r;
    r.name = std::move(name);
    r.external = true;
    r.externalImage = img;
    r.desc = desc;
    r.desc.width = img.width;
    r.desc.height = img.height;
    r.desc.externalImport = true;
    r.firstPass = kInvalidIndex;
    r.lastPass = 0;
    resources_.push_back(std::move(r));
    ++revision_;
    return resourceCount_++;
}

u32 FrameGraph::create_persistent_texture(std::string name, const TextureDesc& desc) noexcept {
    const u32 idx = create_texture(std::move(name), desc);
    if (idx != kInvalidIndex) resources_[idx].persistent = true;
    return idx;
}

u32 FrameGraph::add_pass(std::string name, PassStage stage,
                         PassExecuteFn fn, void* userData) noexcept {
    if (passCount_ >= kMaxPasses) {
        AUREA_LOG_ERROR("FrameGraph: limite de %u passes", kMaxPasses);
        return kInvalidIndex;
    }
    FramePass p;
    p.name = std::move(name);
    p.stage = stage;
    p.execute = fn;
    p.userData = userData;
    passes_.push_back(std::move(p));
    ++revision_;
    return passCount_++;
}

void FrameGraph::pass_reads(u32 passIndex, u32 resourceIndex) noexcept {
    if (passIndex >= passCount_ || resourceIndex >= resourceCount_) return;
    passes_[passIndex].reads.push_back(resourceIndex);
    FrameResource& r = resources_[resourceIndex];
    if (r.firstPass == kInvalidIndex || passIndex < r.firstPass) r.firstPass = passIndex;
    if (passIndex > r.lastPass) r.lastPass = passIndex;
    ++revision_;
}

void FrameGraph::pass_writes(u32 passIndex, u32 resourceIndex) noexcept {
    if (passIndex >= passCount_ || resourceIndex >= resourceCount_) return;
    passes_[passIndex].writes.push_back(resourceIndex);
    FrameResource& r = resources_[resourceIndex];
    if (r.firstPass == kInvalidIndex || passIndex < r.firstPass) r.firstPass = passIndex;
    if (passIndex > r.lastPass) r.lastPass = passIndex;
    ++revision_;
}

void FrameGraph::pass_reads_writes(u32 passIndex, u32 resourceIndex) noexcept {
    pass_reads(passIndex, resourceIndex);
    pass_writes(passIndex, resourceIndex);
}

void FrameGraph::set_output(u32 resourceIndex) noexcept {
    if (resourceIndex >= resourceCount_) return;
    outputs_.push_back(resourceIndex);
    ++revision_;
}

// -----------------------------------------------------------------------------
// Compilação
// -----------------------------------------------------------------------------
Status FrameGraph::compile(GPUBackend& backend) noexcept {
    // Reaproveita o plano anterior quando a estrutura não mudou. Durante
    // playback, 99% dos frames têm exatamente a mesma topologia — recompilar
    // seria trabalho puro. O que muda entre frames são os PARÂMETROS dos
    // passes, não os passes.
    if (compiled_ && compiledRevision_ == revision_) {
        return OkStatus;
    }

    topo_sort();
    if (order_.size() != passCount_) {
        AUREA_LOG_ERROR("FrameGraph: ciclo detectado entre passes "
                        "(ordenados %llu de %u)",
                        static_cast<unsigned long long>(order_.size()), passCount_);
        return Errc::InvalidState;
    }

    cull_unreachable();
    alias_resources();

    // Depois do aliasing, só os slots físicos SEM dono anterior precisam de
    // alocação real. Um slot reaproveitado já tem textura do frame passado.
    for (PhysicalResource& phys : physical_) {
        if (phys.texture.valid() || phys.buffer.valid()) continue;
        if (phys.isBuffer) {
            auto res = backend.create_buffer(phys.desc.width, phys.desc.usageHint);
            if (!res.ok()) {
                AUREA_LOG_ERROR("FrameGraph: falha ao criar buffer '%s'",
                                phys.ownerName.c_str());
                return res.code();
            }
            phys.buffer = *res;
        } else {
            auto res = backend.create_texture(phys.desc);
            if (!res.ok()) {
                AUREA_LOG_ERROR("FrameGraph: falha ao criar textura '%s' (%ux%u)",
                                phys.ownerName.c_str(), phys.desc.width, phys.desc.height);
                return res.code();
            }
            phys.texture = *res;
        }
    }

    // Fixa em cada recurso lógico o handle físico resolvido.
    for (u32 i = 0; i < resourceCount_; ++i) {
        FrameResource& r = resources_[i];
        if (r.external) continue;
        const i32 slot = resourceToPhysical_[i];
        if (slot < 0) continue;
        r.physical = physical_[static_cast<usize>(slot)].texture;
        r.buffer   = physical_[static_cast<usize>(slot)].buffer;
    }

    insert_barriers();

    compiled_ = true;
    compiledRevision_ = revision_;
    return OkStatus;
}

void FrameGraph::topo_sort() noexcept {
    order_.clear();
    marks_.assign(passCount_, 0);

    // Ordenação por dependência: um passe só entra depois de todos os passes
    // que escrevem os recursos que ele lê. Implementado com DFS iterativo para
    // não estourar a pilha num grafo profundo.
    //
    // Um passe que ESCREVE um recurso também depende de quem o escreveu antes
    // (antirraw), senão dois passes escrevendo o mesmo alvo rodariam trocados.
    std::vector<u32> stack;
    stack.reserve(passCount_);

    for (u32 start = 0; start < passCount_; ++start) {
        if (marks_[start] == 2) continue;
        stack.clear();
        stack.push_back(start);

        while (!stack.empty()) {
            const u32 p = stack.back();
            if (marks_[p] == 2) { stack.pop_back(); continue; }

            if (marks_[p] == 0) {
                marks_[p] = 1;   // na recursão
                bool pushed = false;
                for (u32 dep : passes_[p].reads) {
                    for (u32 other = 0; other < passCount_; ++other) {
                        if (other == p || marks_[other] == 2) continue;
                        const FramePass& op = passes_[other];
                        const bool writes = std::find(op.writes.begin(), op.writes.end(), dep)
                                            != op.writes.end();
                        if (!writes) continue;
                        if (marks_[other] == 1) {
                            // Ciclo: `other` já está na pilha. Não é recuperável
                            // — o grafo está errado, e o chamador precisa saber.
                            return;
                        }
                        stack.push_back(other);
                        pushed = true;
                    }
                }
                if (pushed) continue;
            }

            if (marks_[p] == 1) {
                marks_[p] = 2;
                order_.push_back(p);
            }
            stack.pop_back();
        }
    }
}

void FrameGraph::cull_unreachable() noexcept {
    // Um passe sobrevive se algum recurso que ele escreve é lido por um passe
    // já vivo, ou é uma saída. A propagação é de trás para frente: começa nas
    // saídas e sobe pelas dependências.
    std::vector<u8> alive(resourceCount_, 0);
    std::vector<u8> passAlive(passCount_, 0);

    for (u32 out : outputs_) {
        if (out < resourceCount_) alive[out] = 1;
    }

    // Repete até estabilizar. O número de iterações é limitado pela
    // profundidade do grafo — na prática 2 ou 3 num compositor de editor.
    bool changed = true;
    while (changed) {
        changed = false;
        for (u32 p = 0; p < passCount_; ++p) {
            if (passAlive[p]) continue;
            const FramePass& pass = passes_[p];

            // O passe é necessário se escreve algo vivo.
            bool needed = false;
            for (u32 w : pass.writes) {
                if (alive[w]) { needed = true; break; }
            }
            if (!needed) continue;

            passAlive[p] = 1;
            changed = true;

            // Tudo que ele lê passa a estar vivo.
            for (u32 r : pass.reads) {
                if (!alive[r]) { alive[r] = 1; changed = true; }
            }
        }
    }

    culledCount_ = 0;
    for (u32 p = 0; p < passCount_; ++p) {
        passes_[p].culled = (passAlive[p] == 0);
        if (passes_[p].culled) ++culledCount_;
    }

    // Remove os passes podados da ordem de execução.
    order_.erase(std::remove_if(order_.begin(), order_.end(),
                                [&](u32 p) { return passes_[p].culled; }),
                 order_.end());

    // Recursos que ninguém usa não são alocados.
    for (u32 i = 0; i < resourceCount_; ++i) {
        if (!alive[i] && !resources_[i].persistent) {
            resources_[i].firstPass = kInvalidIndex;
            resources_[i].lastPass = 0;
        }
    }
}

void FrameGraph::alias_resources() noexcept {
    // Aliasing de memória: dois recursos com tempos de vida que NÃO se
    // sobrepõem dividem a mesma textura física, desde que a descrição seja
    // compatível.
    //
    // Numa cadeia de 8 efeitos sobre 4K, isso é a diferença entre ~600 MB de
    // pico e ~150 MB — e é literalmente a diferença entre rodar e ser morto
    // pelo sistema num aparelho de 6 GB.
    //
    // A ordem importa: os slots físicos sobrevivem entre frames (realocar 4K a
    // 60 Hz seria o gargalo dominante), então o casamento é estável — o mesmo
    // recurso lógico tende a cair no mesmo físico frame após frame, o que
    // evita recriação desnecessária.
    resourceToPhysical_.assign(resourceCount_, -1);
    physicalCount_ = 0;

    // Nenhum slot está ocupado no início do frame.
    for (auto& p : physical_) p.busyUntilPass = kInvalidIndex;

    auto same_shape = [](const TextureDesc& a, const TextureDesc& b) noexcept {
        return a.width == b.width && a.height == b.height
            && a.format == b.format && a.sampleCount == b.sampleCount
            && a.layers == b.layers && a.mipLevels == b.mipLevels;
    };

    // Percorre em ordem de primeiro uso: assim um recurso é casado com um
    // físico cujo uso terminou, e não com um que ainda vai rodar.
    std::vector<u32> byFirstUse;
    byFirstUse.reserve(resourceCount_);
    for (u32 i = 0; i < resourceCount_; ++i) {
        const FrameResource& r = resources_[i];
        if (r.external) continue;
        if (r.firstPass == kInvalidIndex) continue;   // podado, ninguém usa
        byFirstUse.push_back(i);
    }
    std::sort(byFirstUse.begin(), byFirstUse.end(),
              [this](u32 a, u32 b) { return resources_[a].firstPass < resources_[b].firstPass; });

    for (u32 i : byFirstUse) {
        const FrameResource& r = resources_[i];

        i32 chosen = -1;
        if (!r.persistent) {
            // Recurso de histórico NUNCA é aliado: ele precisa sobreviver ao
            // frame para o efeito temporal poder ler o frame anterior. É a
            // exceção explícita ao esquema, e por isso ela é declarada.
            for (usize s = 0; s < physical_.size(); ++s) {
                PhysicalResource& phys = physical_[s];
                if (phys.isBuffer != r.isBuffer) continue;
                if (!same_shape(phys.desc, r.desc)) continue;
                // Livre se o último uso terminou ANTES deste recurso começar.
                if (phys.busyUntilPass != kInvalidIndex
                    && phys.busyUntilPass >= r.firstPass) {
                    continue;
                }
                chosen = static_cast<i32>(s);
                break;
            }
        }

        if (chosen < 0) {
            PhysicalResource phys;
            phys.desc = r.desc;
            phys.isBuffer = r.isBuffer;
            phys.ownerName = r.name;
            physical_.push_back(std::move(phys));
            chosen = static_cast<i32>(physical_.size()) - 1;
        }

        PhysicalResource& phys = physical_[static_cast<usize>(chosen)];
        phys.busyUntilPass = r.lastPass;
        if (phys.ownerName.empty()) phys.ownerName = r.name;
        resourceToPhysical_[i] = chosen;
        ++physicalCount_;
    }

    // Descarta os físicos que sobraram de frames anteriores com formato que
    // não é mais usado. Sem isso, mudar o modo de preview acumularia texturas
    // de todas as resoluções já usadas.
    // (A destruição efetiva acontece em `release_gpu_resources`, que tem o
    // backend em mãos. Aqui só se marca pelo tamanho.)
    if (physical_.size() > 64) {
        for (auto it = physical_.begin(); it != physical_.end();) {
            if (it->busyUntilPass == kInvalidIndex) {
                it = physical_.erase(it);
            } else {
                ++it;
            }
        }
        // Os índices mudaram com o erase: invalida o mapa e refaz a ligação
        // numa segunda passada simples.
        resourceToPhysical_.assign(resourceCount_, -1);
        for (u32 i : byFirstUse) {
            const FrameResource& r = resources_[i];
            for (usize s = 0; s < physical_.size(); ++s) {
                if (physical_[s].ownerName == r.name
                    && physical_[s].isBuffer == r.isBuffer) {
                    resourceToPhysical_[i] = static_cast<i32>(s);
                    break;
                }
            }
        }
    }
}

void FrameGraph::insert_barriers() noexcept {
    // O backend infere a barreira a partir da transição de layout em
    // `begin_render_pass`. Nada a fazer aqui além de garantir a ordem — que o
    // `reset()` do frame seguinte não deixe um recurso lido como escrito.
    //
    // Este método existe como ponto de extensão explícito: quando o backend
    // ganhar barreiras explícitas de buffer (para compute com dependência de
    // leitura), é aqui que elas entram, sem tocar no resto do grafo.
}

void FrameGraph::execute(CommandList& cmds) noexcept {
    for (u32 p : order_) {
        FramePass& pass = passes_[p];
        if (pass.culled || !pass.execute) continue;
        pass.execute(*this, pass.userData, cmds);
    }
}

TextureHandle FrameGraph::texture(u32 resourceIndex) const noexcept {
    if (resourceIndex >= resourceCount_) return TextureHandle{};
    return resources_[resourceIndex].physical;
}

u32 FrameGraph::find_resource(const char* name) const noexcept {
    if (!name) return kInvalidIndex;
    for (u32 i = 0; i < resourceCount_; ++i) {
        if (resources_[i].name == name) return i;
    }
    return kInvalidIndex;
}

u32 FrameGraph::find_pass(const char* name) const noexcept {
    if (!name) return kInvalidIndex;
    for (u32 i = 0; i < passCount_; ++i) {
        if (passes_[i].name == name) return i;
    }
    return kInvalidIndex;
}

std::string FrameGraph::dump() const {
    std::ostringstream os;
    os << "FrameGraph: " << passCount_ << " passes (" << culledCount_ << " podados), "
       << resourceCount_ << " recursos logicos, " << physicalCount_ << " fisicos\n";
    os << "ordem de execucao:\n";
    for (u32 p : order_) {
        const FramePass& pass = passes_[p];
        os << "  [" << to_string(pass.stage) << "] " << pass.name;
        if (pass.measuredMs > 0.0f) os << " " << pass.measuredMs << " ms";
        os << "\n";
    }
    return os.str();
}

void FrameGraph::release_gpu_resources(GPUBackend& backend) noexcept {
    // Dispositivo perdido: todas as texturas do driver antigo morreram. Os
    // handles do MOTOR continuam válidos (o cache de shader inclusive), mas os
    // recursos físicos precisam ser recriados.
    for (auto& r : resources_) {
        r.physical = TextureHandle{};
        r.buffer = BufferHandle{};
    }
    for (auto& p : physical_) {
        p.texture = TextureHandle{};
        p.buffer = BufferHandle{};
        p.busyUntilPass = kInvalidIndex;
    }
    physical_.clear();
    resourceToPhysical_.clear();
    physicalCount_ = 0;
    compiled_ = false;
    compiledRevision_ = 0;
    (void)backend;
}

} // namespace aurea

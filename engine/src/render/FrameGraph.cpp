#include "aurea/render/FrameGraph.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>
#include <cstdio>

namespace aurea {

// =============================================================================
// PassContext
// =============================================================================
TextureHandle PassContext::texture(FGTexture t) const noexcept { return graph.physical(t); }
const TextureDesc& PassContext::desc(FGTexture t) const noexcept { return graph.desc(t); }

// =============================================================================
// TransientTexturePool
// =============================================================================
void TransientTexturePool::begin_frame(GPUBackend& backend, u64 frameNumber) noexcept {
    backend_ = &backend;
    frame_ = frameNumber;
    stats_.createdThisFrame = 0;
    stats_.destroyedThisFrame = 0;
}

TextureHandle TransientTexturePool::acquire(const TextureDesc& desc) noexcept {
    for (Entry& e : entries_) {
        if (e.inUse || !e.desc.compatible(desc)) continue;
        e.inUse = true;
        e.lastUsedFrame = frame_;
        ++stats_.inUse;
        return e.texture;
    }
    if (!backend_) return TextureHandle{};

    auto created = backend_->create_texture(desc);
    if (!created.ok()) {
        AUREA_LOG_ERROR("pool: falha ao criar textura %ux%u (%s)", desc.width, desc.height,
                        desc.debugName ? desc.debugName : "?");
        return TextureHandle{};
    }
    Entry e;
    e.desc = desc;
    e.desc.debugName = nullptr;   // o nome é do primeiro dono, não da textura
    e.texture = *created;
    e.lastUsedFrame = frame_;
    e.inUse = true;
    entries_.push_back(e);
    ++stats_.alive;
    ++stats_.inUse;
    ++stats_.createdThisFrame;
    ++stats_.createdTotal;
    stats_.bytes += desc.estimated_bytes();
    return e.texture;
}

void TransientTexturePool::release(TextureHandle texture) noexcept {
    for (Entry& e : entries_) {
        if (e.texture == texture && e.inUse) {
            e.inUse = false;
            e.lastUsedFrame = frame_;
            if (stats_.inUse) --stats_.inUse;
            return;
        }
    }
}

void TransientTexturePool::end_frame() noexcept {
    if (!backend_) return;
    for (usize i = 0; i < entries_.size();) {
        Entry& e = entries_[i];
        if (!e.inUse && frame_ > e.lastUsedFrame + idleFrames_) {
            backend_->destroy_texture(e.texture);
            stats_.bytes -= std::min(stats_.bytes, e.desc.estimated_bytes());
            if (stats_.alive) --stats_.alive;
            ++stats_.destroyedThisFrame;
            entries_[i] = entries_.back();
            entries_.pop_back();
            continue;
        }
        ++i;
    }
}

void TransientTexturePool::clear() noexcept {
    if (backend_) {
        for (Entry& e : entries_) backend_->destroy_texture(e.texture);
    }
    forget();
}

void TransientTexturePool::forget() noexcept {
    entries_.clear();
    stats_.alive = 0;
    stats_.inUse = 0;
    stats_.bytes = 0;
}

// =============================================================================
// FrameGraph — declaração
// =============================================================================
FrameGraph::FrameGraph() {
    passes_.reserve(64);
    resources_.reserve(64);
    accesses_.reserve(256);
    sortedAccess_.reserve(256);
    order_.reserve(64);
    slots_.reserve(32);
    barriers_.reserve(256);
}

void FrameGraph::reset() noexcept {
    passes_.clear();
    resources_.clear();
    accesses_.clear();
    sortedAccess_.clear();
    order_.clear();
    outputs_.clear();
    slots_.clear();
    barriers_.clear();
    finalBarriers_.clear();
    stats_ = Stats{};
    compiled_ = false;
}

FGTexture FrameGraph::create_texture(const char* name, const TextureDesc& desc) noexcept {
    if (desc.width == 0 || desc.height == 0) {
        AUREA_LOG_ERROR("FrameGraph: textura '%s' com dimensao zero", name ? name : "?");
        return FGTexture{};
    }
    Resource r;
    r.name = name ? name : "";
    r.desc = desc;
    r.desc.debugName = name;
    resources_.push_back(r);
    return FGTexture{static_cast<u32>(resources_.size() - 1)};
}

FGTexture FrameGraph::import_texture(const char* name, TextureHandle texture,
                                     const TextureDesc& desc) noexcept {
    if (!texture.valid()) return FGTexture{};
    Resource r;
    r.name = name ? name : "";
    r.desc = desc;
    r.physical = texture;
    r.imported = true;
    resources_.push_back(r);
    return FGTexture{static_cast<u32>(resources_.size() - 1)};
}

u32 FrameGraph::add_raster_pass(const char* name, PassStage stage, FGTexture colorTarget,
                                LoadOp load, Vec4 clear, PassFn fn) noexcept {
    Pass p;
    p.name = name ? name : "";
    p.stage = stage;
    p.kind = PassKind::Raster;
    p.colorTarget = colorTarget;
    p.load = load;
    p.clear[0] = clear.x; p.clear[1] = clear.y; p.clear[2] = clear.z; p.clear[3] = clear.w;
    p.fn = std::move(fn);
    passes_.push_back(std::move(p));
    const u32 index = static_cast<u32>(passes_.size() - 1);
    if (colorTarget.valid()) {
        // Carregar o conteúdo anterior é uma LEITURA: quem escreveu antes
        // precisa rodar antes, e não pode ser podado.
        if (load == LoadOp::Load) add_access(index, colorTarget, Access::Read);
        add_access(index, colorTarget, Access::ColorWrite);
    }
    return index;
}

u32 FrameGraph::add_raster_pass_depth(const char* name, PassStage stage, FGTexture colorTarget, LoadOp load,
                                      Vec4 clear, FGTexture depthTarget, LoadOp depthLoad, bool storeDepth,
                                      f32 clearDepth, PassFn fn) noexcept {
    const u32 index = add_raster_pass(name, stage, colorTarget, load, clear, std::move(fn));
    Pass& p = passes_[index];
    p.depthTarget = depthTarget;
    p.depthLoad = depthLoad;
    p.storeDepth = storeDepth;
    p.clearDepth = clearDepth;
    if (depthTarget.valid()) {
        if (depthLoad == LoadOp::Load) add_access(index, depthTarget, Access::Read);
        add_access(index, depthTarget, Access::DepthWrite);
    }
    return index;
}

u32 FrameGraph::add_compute_pass(const char* name, PassStage stage, PassFn fn) noexcept {
    Pass p;
    p.name = name ? name : "";
    p.stage = stage;
    p.kind = PassKind::Compute;
    p.fn = std::move(fn);
    passes_.push_back(std::move(p));
    return static_cast<u32>(passes_.size() - 1);
}

u32 FrameGraph::add_transfer_pass(const char* name, PassStage stage, PassFn fn) noexcept {
    Pass p;
    p.name = name ? name : "";
    p.stage = stage;
    p.kind = PassKind::Transfer;
    p.fn = std::move(fn);
    passes_.push_back(std::move(p));
    return static_cast<u32>(passes_.size() - 1);
}

void FrameGraph::add_access(u32 pass, FGTexture t, Access a) noexcept {
    if (pass >= passes_.size() || !t.valid() || t.index >= resources_.size()) return;
    accesses_.push_back(AccessRecord{pass, t.index, a});
}

void FrameGraph::read(u32 pass, FGTexture t) noexcept { add_access(pass, t, Access::Read); }
void FrameGraph::write_storage(u32 pass, FGTexture t) noexcept { add_access(pass, t, Access::StorageWrite); }
void FrameGraph::copy_source(u32 pass, FGTexture t) noexcept { add_access(pass, t, Access::CopySrc); }
void FrameGraph::copy_destination(u32 pass, FGTexture t) noexcept { add_access(pass, t, Access::CopyDst); }

void FrameGraph::mark_side_effect(u32 pass) noexcept {
    if (pass < passes_.size()) passes_[pass].sideEffect = true;
}

void FrameGraph::set_output(FGTexture t, ResourceState finalState) noexcept {
    if (!t.valid() || t.index >= resources_.size()) return;
    resources_[t.index].isOutput = true;
    resources_[t.index].finalState = finalState;
    outputs_.push_back(t.index);
}

// =============================================================================
// Compilação
// =============================================================================
namespace {
[[nodiscard]] constexpr bool is_write(u8 a) noexcept { return a != 0 && a != 3; }   // Read=0, CopySrc=3
}

Status FrameGraph::compile(TransientTexturePool& pool) noexcept {
    stats_ = Stats{};
    stats_.passesDeclared = static_cast<u32>(passes_.size());

    // Acessos agrupados por passe (contagem: estável — a ordem de declaração
    // dentro do passe é preservada — e sem o buffer que o stable_sort aloca).
    const u32 passCount = static_cast<u32>(passes_.size());
    passBucket_.assign(passCount + 1, 0);
    for (const AccessRecord& a : accesses_) ++passBucket_[a.pass + 1];
    for (u32 i = 0; i < passCount; ++i) passBucket_[i + 1] += passBucket_[i];
    sortedAccess_.resize(accesses_.size());
    fill_.assign(passBucket_.begin(), passBucket_.end() - 1);
    for (const AccessRecord& a : accesses_) sortedAccess_[fill_[a.pass]++] = a;
    for (u32 i = 0; i < passCount; ++i) {
        Pass& p = passes_[i];
        p.accessBegin = passBucket_[i];
        p.accessCount = passBucket_[i + 1] - passBucket_[i];
        p.culled = false;
    }

    if (!sort_passes()) {
        AUREA_LOG_ERROR("FrameGraph: ciclo entre passes");
        return Status{Errc::InvalidState, "ciclo no FrameGraph"};
    }
    cull();

    // Vidas: posição do primeiro e do último uso na ordem de execução.
    for (Resource& r : resources_) {
        r.firstUse = kInvalidIndex;
        r.lastUse = kInvalidIndex;
        r.slot = kInvalidIndex;
        if (!r.imported) r.physical = TextureHandle{};
    }
    for (u32 pos = 0; pos < order_.size(); ++pos) {
        const Pass& p = passes_[order_[pos]];
        for (u32 k = 0; k < p.accessCount; ++k) {
            Resource& r = resources_[sortedAccess_[p.accessBegin + k].resource];
            if (r.firstUse == kInvalidIndex) r.firstUse = pos;
            r.lastUse = pos;
        }
    }

    // O primeiro uso de uma textura transitória TEM que ser escrita. Ler antes
    // de escrever é lixo de memória na tela — e em alguns drivers, lixo de
    // OUTRO app.
    for (u32 i = 0; i < resources_.size(); ++i) {
        const Resource& r = resources_[i];
        if (r.imported || r.firstUse == kInvalidIndex) continue;
        const Pass& p = passes_[order_[r.firstUse]];
        bool written = false;
        for (u32 k = 0; k < p.accessCount; ++k) {
            const AccessRecord& a = sortedAccess_[p.accessBegin + k];
            if (a.resource != i) continue;
            if (a.access == Access::Read || a.access == Access::CopySrc) {
                written = false;
                break;
            }
            written = true;
        }
        if (!written) {
            AUREA_LOG_ERROR("FrameGraph: '%s' lida no passe '%s' antes de ser escrita",
                            r.name, p.name);
            return Status{Errc::InvalidState, "textura lida antes de escrita"};
        }
    }

    if (const Status s = assign_physical(pool); !s.ok()) return s;
    plan_barriers();

    stats_.passesExecuted = static_cast<u32>(order_.size());
    stats_.passesCulled = stats_.passesDeclared - stats_.passesExecuted;
    stats_.physicalTextures = static_cast<u32>(slots_.size());
    for (const Slot& s : slots_) stats_.transientBytes += s.desc.estimated_bytes();
    stats_.barriers = static_cast<u32>(barriers_.size() + finalBarriers_.size());

    compiled_ = true;
    return OkStatus;
}

bool FrameGraph::sort_passes() noexcept {
    const u32 n = static_cast<u32>(passes_.size());
    order_.clear();
    if (n == 0) return true;

    // Arestas pred → succ, em pares achatados (pred, succ).
    //
    // Cada ESCRITA cria uma versão nova do recurso (como SSA). Uma leitura lê a
    // versão do último escritor declarado ANTES dela — ou, se não houver
    // nenhum antes, a do primeiro declarado depois (o produtor foi declarado
    // mais tarde, e é isso que "ordem por dependência" permite). Uma escrita
    // que não é a primeira depende do escritor anterior (escrita sobre
    // escrita) e de quem leu a versão anterior (leitura antes de escrita).
    //
    // Por recurso (CSR na ordem dos passes), não por varredura global: cada
    // acesso é visitado um número constante de vezes (mais a busca binária no
    // punhado de escritores do recurso).
    edges_.clear();
    const u32 accessCount = static_cast<u32>(sortedAccess_.size());
    const u32 resourceCount = static_cast<u32>(resources_.size());
    resBegin_.assign(resourceCount + 1, 0);
    for (u32 i = 0; i < accessCount; ++i) ++resBegin_[sortedAccess_[i].resource + 1];
    for (u32 r = 0; r < resourceCount; ++r) resBegin_[r + 1] += resBegin_[r];
    resAccess_.resize(accessCount);
    fill_.assign(resBegin_.begin(), resBegin_.end() - 1);
    for (u32 i = 0; i < accessCount; ++i) resAccess_[fill_[sortedAccess_[i].resource]++] = i;
    producer_.assign(accessCount, kInvalidIndex);
    for (u32 r = 0; r < resourceCount; ++r) {
        const u32 b = resBegin_[r], e = resBegin_[r + 1];
        writers_.clear();
        for (u32 k = b; k < e; ++k) {
            const AccessRecord& a = sortedAccess_[resAccess_[k]];
            if (is_write(static_cast<u8>(a.access)) && (writers_.empty() || writers_.back() != a.pass)) writers_.push_back(a.pass);
        }
        if (writers_.empty()) continue;
        // Leitura: o último escritor ANTES dela; sem nenhum, o primeiro depois
        // (nunca o próprio passe).
        for (u32 k = b; k < e; ++k) {
            const AccessRecord& a = sortedAccess_[resAccess_[k]];
            if (is_write(static_cast<u8>(a.access))) continue;
            auto it = std::lower_bound(writers_.begin(), writers_.end(), a.pass);
            u32 prod = kInvalidIndex;
            if (it != writers_.begin()) {
                prod = *(it - 1);
            } else {
                if (it != writers_.end() && *it == a.pass) ++it;
                if (it != writers_.end()) prod = *it;
            }
            producer_[resAccess_[k]] = prod;
            if (prod != kInvalidIndex) { edges_.push_back(prod); edges_.push_back(a.pass); }
        }
        // Escrita que não é a primeira: depois do escritor anterior e de
        // quem leu a versão dele.
        for (usize w = 1; w < writers_.size(); ++w) {
            const u32 prev = writers_[w - 1], pass = writers_[w];
            edges_.push_back(prev);
            edges_.push_back(pass);
            for (u32 k = b; k < e; ++k) {
                const AccessRecord& a = sortedAccess_[resAccess_[k]];
                if (a.pass == pass || is_write(static_cast<u8>(a.access))) continue;
                if (producer_[resAccess_[k]] == prev) { edges_.push_back(a.pass); edges_.push_back(pass); }
            }
        }
    }

    // CSR a partir dos pares.
    edgeBegin_.assign(n + 1, 0);
    indegree_.assign(n, 0);
    for (usize e = 0; e + 1 < edges_.size(); e += 2) ++edgeBegin_[edges_[e] + 1];
    for (u32 i = 0; i < n; ++i) edgeBegin_[i + 1] += edgeBegin_[i];
    succ_.assign(edgeBegin_.back(), 0);
    fill_.assign(edgeBegin_.begin(), edgeBegin_.end() - 1);
    for (usize e = 0; e + 1 < edges_.size(); e += 2) {
        succ_[fill_[edges_[e]]++] = edges_[e + 1];
        ++indegree_[edges_[e + 1]];
    }

    // Kahn com desempate pelo menor índice de declaração: a ordem é
    // determinística, e igual à de declaração quando ela já respeita as
    // dependências (o caso normal). Arestas duplicadas são inofensivas: cada
    // uma soma e subtrai um do grau de entrada.
    // Heap mínimo: a varredura linear da fila era quadrática com centenas de
    // passes independentes (uma fonte por camada).
    queue_.clear();
    const auto later = [](u32 a, u32 b) noexcept { return a > b; };
    for (u32 i = 0; i < n; ++i) if (indegree_[i] == 0) queue_.push_back(i);
    std::make_heap(queue_.begin(), queue_.end(), later);

    while (!queue_.empty()) {
        std::pop_heap(queue_.begin(), queue_.end(), later);
        const u32 p = queue_.back();
        queue_.pop_back();
        order_.push_back(p);
        for (u32 e = edgeBegin_[p]; e < edgeBegin_[p + 1]; ++e) {
            const u32 s = succ_[e];
            if (--indegree_[s] == 0) {
                queue_.push_back(s);
                std::push_heap(queue_.begin(), queue_.end(), later);
            }
        }
    }
    return order_.size() == n;
}

void FrameGraph::cull() noexcept {
    for (Resource& r : resources_) r.alive = false;
    for (u32 out : outputs_) resources_[out].alive = true;

    for (usize idx = order_.size(); idx-- > 0;) {
        Pass& p = passes_[order_[idx]];
        bool needed = p.sideEffect;
        for (u32 k = 0; k < p.accessCount && !needed; ++k) {
            const AccessRecord& a = sortedAccess_[p.accessBegin + k];
            if (is_write(static_cast<u8>(a.access)) && resources_[a.resource].alive) needed = true;
        }
        p.culled = !needed;
        if (!needed) continue;
        for (u32 k = 0; k < p.accessCount; ++k) {
            const AccessRecord& a = sortedAccess_[p.accessBegin + k];
            if (!is_write(static_cast<u8>(a.access))) resources_[a.resource].alive = true;
        }
    }
    order_.erase(std::remove_if(order_.begin(), order_.end(),
                                [this](u32 p) { return passes_[p].culled; }),
                 order_.end());
}

Status FrameGraph::assign_physical(TransientTexturePool& pool) noexcept {
    slots_.clear();
    const u32 steps = static_cast<u32>(order_.size());
    const u32 resourceCount = static_cast<u32>(resources_.size());
    // Quem nasce e quem morre em cada posição (CSR, na ordem dos índices —
    // a mesma escolha de física que a varredura completa fazia).
    bornBegin_.assign(steps + 1, 0);
    diesBegin_.assign(steps + 1, 0);
    for (const Resource& r : resources_) {
        if (r.imported || r.firstUse == kInvalidIndex) continue;
        ++bornBegin_[r.firstUse + 1];
        if (!r.isOutput) ++diesBegin_[r.lastUse + 1];
    }
    for (u32 s = 0; s < steps; ++s) {
        bornBegin_[s + 1] += bornBegin_[s];
        diesBegin_[s + 1] += diesBegin_[s];
    }
    born_.resize(bornBegin_[steps]);
    dies_.resize(diesBegin_[steps]);
    fill_.assign(bornBegin_.begin(), bornBegin_.end() - 1);
    for (u32 i = 0; i < resourceCount; ++i) {
        const Resource& r = resources_[i];
        if (r.imported || r.firstUse == kInvalidIndex) continue;
        born_[fill_[r.firstUse]++] = i;
    }
    fill_.assign(diesBegin_.begin(), diesBegin_.end() - 1);
    for (u32 i = 0; i < resourceCount; ++i) {
        const Resource& r = resources_[i];
        if (r.imported || r.firstUse == kInvalidIndex || r.isOutput) continue;
        dies_[fill_[r.lastUse]++] = i;
    }

    for (u32 pos = 0; pos < steps; ++pos) {
        // Nasce aqui: pega uma física livre compatível deste frame, senão o pool.
        for (u32 bi = bornBegin_[pos]; bi < bornBegin_[pos + 1]; ++bi) {
            Resource& r = resources_[born_[bi]];
            ++stats_.transientTextures;

            u32 chosen = kInvalidIndex;
            for (u32 s = 0; s < slots_.size(); ++s) {
                if (slots_[s].free && slots_[s].desc.compatible(r.desc)) { chosen = s; break; }
            }
            if (chosen != kInvalidIndex) {
                ++stats_.aliasedTextures;
                slots_[chosen].free = false;
            } else {
                const TextureHandle t = pool.acquire(r.desc);
                if (!t.valid()) {
                    return Status{Errc::OutOfDeviceMemory, "textura transitoria indisponivel"};
                }
                Slot slot;
                slot.texture = t;
                slot.desc = r.desc;
                slots_.push_back(slot);
                chosen = static_cast<u32>(slots_.size() - 1);
            }
            r.slot = chosen;
            r.physical = slots_[chosen].texture;
        }
        // Morre aqui: a física volta a ficar livre para quem nascer depois.
        // Saídas nunca morrem dentro do frame.
        for (u32 di = diesBegin_[pos]; di < diesBegin_[pos + 1]; ++di) {
            const Resource& r = resources_[dies_[di]];
            if (r.slot == kInvalidIndex) continue;
            slots_[r.slot].free = true;
        }
    }
    return OkStatus;
}

void FrameGraph::plan_barriers() noexcept {
    barriers_.clear();
    finalBarriers_.clear();
    tracked_.assign(resources_.size(), ResourceState::Undefined);

    for (u32 pos = 0; pos < order_.size(); ++pos) {
        Pass& p = passes_[order_[pos]];
        p.barrierBegin = static_cast<u32>(barriers_.size());
        p.barrierCount = 0;

        for (u32 k = 0; k < p.accessCount; ++k) {
            const AccessRecord& a = sortedAccess_[p.accessBegin + k];
            const Resource& r = resources_[a.resource];
            ResourceState want = ResourceState::ShaderRead;
            bool discard = false;
            switch (a.access) {
                case Access::Read:
                    // Leitura do alvo do próprio passe (LoadOp::Load) é a carga do
                    // render pass, não amostragem: o estado certo é o de anexo.
                    if (p.kind == PassKind::Raster
                        && (p.colorTarget.index == a.resource || p.depthTarget.index == a.resource)) continue;
                    want = ResourceState::ShaderRead;
                    break;
                case Access::ColorWrite:
                    want = ResourceState::ColorAttachment;
                    discard = p.load != LoadOp::Load;
                    break;
                case Access::StorageWrite:
                    want = ResourceState::StorageWrite;
                    discard = !r.imported && r.firstUse == pos;
                    break;
                case Access::CopySrc:
                    want = ResourceState::TransferSrc;
                    break;
                case Access::DepthWrite:
                    want = ResourceState::DepthAttachment;
                    discard = p.depthLoad != LoadOp::Load;
                    break;
                case Access::CopyDst:
                    want = ResourceState::TransferDst;
                    discard = !r.imported && r.firstUse == pos;
                    break;
            }
            if (tracked_[a.resource] == want && !discard) continue;
            barriers_.push_back(PlannedBarrier{a.resource, want, discard});
            tracked_[a.resource] = want;
            ++p.barrierCount;
        }
    }

    for (u32 out : outputs_) {
        const Resource& r = resources_[out];
        if (r.firstUse == kInvalidIndex) continue;
        if (tracked_[out] == r.finalState) continue;
        finalBarriers_.push_back(PlannedBarrier{out, r.finalState, false});
        tracked_[out] = r.finalState;
    }
}

// =============================================================================
// Execução
// =============================================================================
void FrameGraph::execute(CommandList& cmds, bool timers) noexcept {
    if (!compiled_) return;
    PassContext ctx{cmds, *this};

    for (u32 pos = 0; pos < order_.size(); ++pos) {
        Pass& p = passes_[order_[pos]];

        for (u32 b = 0; b < p.barrierCount; ++b) {
            const PlannedBarrier& pb = barriers_[p.barrierBegin + b];
            cmds.barrier(resources_[pb.resource].physical, pb.state, pb.discard);
        }

        cmds.begin_label(p.name);
        if (timers) cmds.begin_timer(p.name);

        if (p.kind == PassKind::Raster && (p.colorTarget.valid() || p.depthTarget.valid())) {
            RenderPassBegin rp;
            if (p.colorTarget.valid()) rp.color = resources_[p.colorTarget.index].physical;
            rp.load = p.load;
            if (p.depthTarget.valid()) {
                rp.depth = resources_[p.depthTarget.index].physical;
                rp.depthLoad = p.depthLoad;
                rp.storeDepth = p.storeDepth;
                rp.clearDepth = p.clearDepth;
            }
            for (int c = 0; c < 4; ++c) rp.clear[c] = p.clear[c];
            cmds.begin_render_pass(rp);
            if (p.fn) p.fn(ctx);
            cmds.end_render_pass();
        } else if (p.fn) {
            p.fn(ctx);
        }

        if (timers) cmds.end_timer();
        cmds.end_label();
    }

    for (const PlannedBarrier& pb : finalBarriers_) {
        cmds.barrier(resources_[pb.resource].physical, pb.state, false);
    }
}

void FrameGraph::release(TransientTexturePool& pool) noexcept {
    for (const Slot& s : slots_) pool.release(s.texture);
    slots_.clear();
    for (Resource& r : resources_) {
        if (!r.imported) r.physical = TextureHandle{};
    }
    compiled_ = false;
}

// =============================================================================
// Consultas
// =============================================================================
TextureHandle FrameGraph::physical(FGTexture t) const noexcept {
    if (!t.valid() || t.index >= resources_.size()) return TextureHandle{};
    return resources_[t.index].physical;
}

const TextureDesc& FrameGraph::desc(FGTexture t) const noexcept {
    static const TextureDesc kEmpty{};
    if (!t.valid() || t.index >= resources_.size()) return kEmpty;
    return resources_[t.index].desc;
}

u32 FrameGraph::physical_slot(FGTexture t) const noexcept {
    if (!t.valid() || t.index >= resources_.size()) return kInvalidIndex;
    return resources_[t.index].slot;
}

void FrameGraph::barriers_before(u32 p, std::vector<PlannedBarrier>& out) const {
    out.clear();
    if (p >= passes_.size() || passes_[p].culled) return;
    const Pass& pass = passes_[p];
    for (u32 b = 0; b < pass.barrierCount; ++b) out.push_back(barriers_[pass.barrierBegin + b]);
}

std::string FrameGraph::dump() const {
    std::string s;
    char line[256];
    std::snprintf(line, sizeof(line),
                  "FrameGraph: %u passes (%u podados), %u transitorias em %u fisicas "
                  "(%u reaproveitadas), %u barreiras, %.1f MB\n",
                  stats_.passesDeclared, stats_.passesCulled, stats_.transientTextures,
                  stats_.physicalTextures, stats_.aliasedTextures, stats_.barriers,
                  static_cast<f64>(stats_.transientBytes) / (1024.0 * 1024.0));
    s += line;
    for (u32 pos = 0; pos < order_.size(); ++pos) {
        const Pass& p = passes_[order_[pos]];
        std::snprintf(line, sizeof(line), "  %2u [%s] %s\n", pos, to_string(p.stage), p.name);
        s += line;
    }
    return s;
}

} // namespace aurea

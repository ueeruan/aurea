#include "aurea/memory/MemoryManager.hpp"
#include "aurea/core/Log.hpp"

namespace aurea {
namespace {
constexpr u8 idx(MemoryClass c) noexcept { return static_cast<u8>(c); }

static_assert([] {
    u32 s = 0;
    for (u16 v : kBudgetShare) s += v;
    return s == 1000;
}(), "kBudgetShare precisa somar 1000 (100%)");
} // namespace

TrimStage IMemoryReclaimable::trim_stage() const noexcept {
    // Padrão por categoria; um cache pode dizer outro (o de blocos de áudio
    // entra junto com os quadros decodificados, por exemplo).
    switch (memory_class()) {
        case MemoryClass::Thumbnails:     return TrimStage::OffscreenThumbnails;
        case MemoryClass::Waveforms:      return TrimStage::OldWaveforms;
        case MemoryClass::DecodedFrames:  return TrimStage::UnusedDecodedFrames;
        case MemoryClass::Audio:          return TrimStage::UnusedDecodedFrames;
        case MemoryClass::RenderedFrames: return TrimStage::OldRenderCache;
        case MemoryClass::GpuTextures:    return TrimStage::HighMips;
        case MemoryClass::GpuGeometry:    return TrimStage::Unused3DAssets;
        case MemoryClass::Proxies:
        case MemoryClass::Assets:
        case MemoryClass::Export:         return TrimStage::Temporaries;
        case MemoryClass::Persistent:
        case MemoryClass::_Count:         break;
    }
    return TrimStage::None;
}

// -----------------------------------------------------------------------------
// Reservation
// -----------------------------------------------------------------------------
void Reservation::release() noexcept {
    if (mgr_ && bytes_) {
        mgr_->free(cls_, bytes_);
    }
    mgr_ = nullptr;
    bytes_ = 0;
}

void Reservation::resize(usize newBytes) noexcept {
    if (!mgr_) return;
    if (newBytes > bytes_) {
        mgr_->commit(cls_, newBytes - bytes_);
    } else if (newBytes < bytes_) {
        mgr_->free(cls_, bytes_ - newBytes);
    }
    bytes_ = newBytes;
}

// -----------------------------------------------------------------------------
// MemoryManager
// -----------------------------------------------------------------------------
void MemoryManager::apply_budget_table(u64 totalBytes) noexcept {
    for (u8 i = 0; i < kClassCount; ++i) {
        budgets_[i].store(static_cast<usize>(totalBytes / 1000 * kBudgetShare[i]), std::memory_order_relaxed);
    }
}

Reservation MemoryManager::try_reserve(MemoryClass cls, usize bytes) noexcept {
    // O projeto aberto nunca é recusado: se ele não couber, o orçamento está
    // mal dimensionado, e falhar aqui perderia trabalho do usuário.
    if (cls == MemoryClass::Persistent) {
        commit(cls, bytes);
        return Reservation(this, cls, bytes);
    }

    const usize budget = budgets_[idx(cls)].load(std::memory_order_relaxed);
    const usize usedNow = used_[idx(cls)].load(std::memory_order_relaxed);
    if (usedNow + bytes <= budget) {
        commit(cls, bytes);
        return Reservation(this, cls, bytes);
    }

    // Não cabe. Antes de recusar, pede aos caches da MESMA categoria que
    // liberem. (Antes da Fase 8 o pedido ia a todas as categorias: soltava
    // miniatura para "abrir espaço" num contador de quadros que não mudava.)
    const usize needed = usedNow + bytes - budget;
    (void)reclaim_class(cls, needed);

    const usize usedAfter = used_[idx(cls)].load(std::memory_order_relaxed);
    if (usedAfter + bytes <= budget) {
        commit(cls, bytes);
        return Reservation(this, cls, bytes);
    }

    rejections_.fetch_add(1, std::memory_order_relaxed);
    AUREA_LOG_WARN("orcamento de %s recusou %llu bytes (usado %llu de %llu)",
                   to_string(cls),
                   static_cast<unsigned long long>(bytes),
                   static_cast<unsigned long long>(usedAfter),
                   static_cast<unsigned long long>(budget));
    return Reservation{};
}

Reservation MemoryManager::reserve_critical(MemoryClass cls, usize bytes) noexcept {
    // Reserva crítica pode invadir o orçamento de OUTRAS categorias: o frame
    // atual não pode falhar. Ela é usada só pelo compositor, com buffers cujo
    // tamanho é conhecido de antemão — não é uma porta aberta para alocação
    // descontrolada. Antes de invadir, pede aos caches (todas as categorias,
    // na ordem de descarte) que abram o espaço.
    const usize budget = budgets_[idx(cls)].load(std::memory_order_relaxed);
    const usize usedNow = used_[idx(cls)].load(std::memory_order_relaxed);
    if (usedNow + bytes > budget) {
        const usize over = usedNow + bytes - budget;
        request_reclaim(over);
    }
    commit(cls, bytes);
    return Reservation(this, cls, bytes);
}

void MemoryManager::commit(MemoryClass cls, usize bytes) noexcept {
    const usize now = used_[idx(cls)].fetch_add(bytes, std::memory_order_relaxed) + bytes;
    usize p = peak_[idx(cls)].load(std::memory_order_relaxed);
    while (now > p && !peak_[idx(cls)].compare_exchange_weak(p, now, std::memory_order_relaxed)) {}
}

void MemoryManager::free(MemoryClass cls, usize bytes) noexcept {
    // fetch_sub com piso: uma liberação a mais é bug de contabilidade, e deixar
    // o contador estourar para 18 quintilhões esconderia o bug para sempre.
    usize current = used_[idx(cls)].load(std::memory_order_relaxed);
    for (;;) {
        const usize next = current >= bytes ? current - bytes : 0;
        if (used_[idx(cls)].compare_exchange_weak(current, next,
                                                  std::memory_order_relaxed,
                                                  std::memory_order_relaxed)) {
            break;
        }
    }
}

usize MemoryManager::reclaim_locked(MemoryClass cls, usize bytes) noexcept {
    usize freed = 0;
    for (u32 i = 0; i < reclaimableCount_ && freed < bytes; ++i) {
        IMemoryReclaimable* r = reclaimables_[i];
        if (!r || r->memory_class() != cls) continue;
        const usize want = bytes == kReclaimAll ? kReclaimAll : bytes - freed;
        const usize got = r->reclaim(want);
        // O contador da categoria é descontado AQUI, a menos que o cache conte
        // os próprios bytes. O contrato está em IMemoryReclaimable::reclaim.
        if (got && !r->accounts_itself()) free(cls, got);
        freed += got;
    }
    return freed;
}

usize MemoryManager::reclaim_class(MemoryClass cls, usize bytes) noexcept {
    std::lock_guard<std::mutex> lock(registryMutex_);
    return reclaim_locked(cls, bytes);
}

usize MemoryManager::request_reclaim(usize bytes) noexcept {
    // Ordem de descarte: as categorias mais baratas de refazer primeiro.
    // Miniaturas custam um decode pequeno; frames decodificados custam um
    // decode grande; geometria custa um re-upload.
    static constexpr MemoryClass kOrder[] = {
        MemoryClass::Thumbnails,
        MemoryClass::Waveforms,
        MemoryClass::DecodedFrames,
        MemoryClass::RenderedFrames,
        MemoryClass::GpuTextures,
        MemoryClass::GpuGeometry,
        MemoryClass::Audio,
        MemoryClass::Proxies,
        MemoryClass::Assets,
        MemoryClass::Export,
    };
    std::lock_guard<std::mutex> lock(registryMutex_);
    usize freed = 0;
    for (MemoryClass cls : kOrder) {
        if (freed >= bytes) break;
        freed += reclaim_locked(cls, bytes - freed);
    }
    return freed;
}

MemoryManager::TrimReport MemoryManager::trim(TrimStage upTo) noexcept {
    TrimReport rep;
    rep.upTo = upTo;
    if (upTo != TrimStage::None) {
        std::lock_guard<std::mutex> lock(registryMutex_);
        for (u8 st = 1; st <= static_cast<u8>(upTo) && st < static_cast<u8>(TrimStage::_Count); ++st) {
            for (u32 i = 0; i < reclaimableCount_; ++i) {
                IMemoryReclaimable* r = reclaimables_[i];
                if (!r || static_cast<u8>(r->trim_stage()) != st) continue;
                const usize got = r->reclaim(kReclaimAll);
                if (got && !r->accounts_itself()) free(r->memory_class(), got);
                rep.freed[st] += got;
                rep.total += got;
            }
        }
    }
    trims_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> t(trimMutex_);
        lastTrim_ = rep;
    }
    AUREA_LOG_INFO("memoria: trim ate '%s' liberou %llu KB nos caches", to_string(upTo),
                   static_cast<unsigned long long>(rep.total / 1024));
    return rep;
}

void MemoryManager::note_trim_freed(TrimStage stage, u64 bytes) noexcept {
    std::lock_guard<std::mutex> t(trimMutex_);
    lastTrim_.freed[static_cast<u8>(stage)] += bytes;
    lastTrim_.total += bytes;
}

MemoryManager::TrimReport MemoryManager::last_trim() const noexcept {
    std::lock_guard<std::mutex> t(trimMutex_);
    return lastTrim_;
}

usize MemoryManager::balance() noexcept {
    usize freed = 0;
    for (u8 i = 0; i < kClassCount; ++i) {
        if (static_cast<MemoryClass>(i) == MemoryClass::Persistent) continue;
        const usize u = used_[i].load(std::memory_order_relaxed);
        const usize b = budgets_[i].load(std::memory_order_relaxed);
        if (b == 0 || u <= b) continue;
        freed += reclaim_class(static_cast<MemoryClass>(i), u - b);
    }
    return freed;
}

Status MemoryManager::register_reclaimable(IMemoryReclaimable* r) noexcept {
    if (!r) return Errc::InvalidArgument;
    if (r->memory_class() == MemoryClass::Persistent) {
        // O projeto não é descartável. Registrar seria prometer que ele pode
        // ser liberado sob pressão — não pode.
        return Errc::InvalidArgument;
    }
    std::lock_guard<std::mutex> lock(registryMutex_);
    if (reclaimableCount_ >= kMaxReclaimables) return Errc::OutOfRange;
    for (u32 i = 0; i < reclaimableCount_; ++i) {
        if (reclaimables_[i] == r) return Errc::AlreadyExists;
    }
    reclaimables_[reclaimableCount_++] = r;
    return OkStatus;
}

void MemoryManager::unregister_reclaimable(IMemoryReclaimable* r) noexcept {
    // O lock também espera um trim/reclaim em curso terminar: depois desta
    // chamada o cache pode ser destruído sem ninguém dentro dele.
    std::lock_guard<std::mutex> lock(registryMutex_);
    for (u32 i = 0; i < reclaimableCount_; ++i) {
        if (reclaimables_[i] != r) continue;
        // Troca com o último: a ordem não importa, o descarte é por categoria.
        reclaimables_[i] = reclaimables_[reclaimableCount_ - 1];
        reclaimables_[reclaimableCount_ - 1] = nullptr;
        --reclaimableCount_;
        return;
    }
}

u32 MemoryManager::reclaimable_count() const noexcept {
    std::lock_guard<std::mutex> lock(registryMutex_);
    return reclaimableCount_;
}

u32 MemoryManager::collect_metrics(CacheMetrics* out, u32 capacity) const noexcept {
    if (!out || capacity == 0) return 0;
    std::lock_guard<std::mutex> lock(registryMutex_);
    u32 n = 0;
    for (u32 i = 0; i < reclaimableCount_ && n < capacity; ++i) {
        CacheMetrics m;
        if (!reclaimables_[i] || !reclaimables_[i]->metrics(m)) continue;
        out[n++] = m;
    }
    return n;
}

} // namespace aurea

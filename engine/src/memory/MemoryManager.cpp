#include "aurea/memory/MemoryManager.hpp"
#include "aurea/core/Log.hpp"

namespace aurea {
namespace {
constexpr u8 idx(MemoryClass c) noexcept { return static_cast<u8>(c); }
} // namespace

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
Reservation MemoryManager::try_reserve(MemoryClass cls, usize bytes) noexcept {
    // O projeto aberto nunca é recusado: se ele não couber, o orçamento está
    // mal dimensionado, e falhar aqui perderia trabalho do usuário.
    if (cls == MemoryClass::Persistent) {
        commit(cls, bytes);
        return Reservation(this, cls, bytes);
    }

    const usize usedNow = used_[idx(cls)].load(std::memory_order_relaxed);
    if (usedNow + bytes <= budgets_[idx(cls)]) {
        commit(cls, bytes);
        return Reservation(this, cls, bytes);
    }

    // Não cabe. Antes de recusar, pede aos caches da MESMA categoria que
    // liberem. Recusar sem tentar seria deixar memória ocupada por cache
    // enquanto o trabalho real falha.
    const usize needed = usedNow + bytes - budgets_[idx(cls)];
    const usize freed = request_reclaim(needed);

    const usize usedAfter = used_[idx(cls)].load(std::memory_order_relaxed);
    if (usedAfter + bytes <= budgets_[idx(cls)]) {
        commit(cls, bytes);
        return Reservation(this, cls, bytes);
    }

    (void)freed;
    rejections_.fetch_add(1, std::memory_order_relaxed);
    AUREA_LOG_WARN("orcamento de %s recusou %llu bytes (usado %llu de %llu)",
                   to_string(cls),
                   static_cast<unsigned long long>(bytes),
                   static_cast<unsigned long long>(usedAfter),
                   static_cast<unsigned long long>(budgets_[idx(cls)]));
    return Reservation{};
}

Reservation MemoryManager::reserve_critical(MemoryClass cls, usize bytes) noexcept {
    // Reserva crítica pode invadir o orçamento de OUTRAS categorias: o frame
    // atual não pode falhar. Ela é usada só pelo compositor, com buffers cujo
    // tamanho é conhecido de antemão — não é uma porta aberta para alocação
    // descontrolada.
    const usize usedNow = used_[idx(cls)].load(std::memory_order_relaxed);
    if (usedNow + bytes > budgets_[idx(cls)]) {
        const usize over = usedNow + bytes - budgets_[idx(cls)];
        request_reclaim(over);
    }
    commit(cls, bytes);
    return Reservation(this, cls, bytes);
}

void MemoryManager::commit(MemoryClass cls, usize bytes) noexcept {
    used_[idx(cls)].fetch_add(bytes, std::memory_order_relaxed);
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

usize MemoryManager::request_reclaim(usize bytes) noexcept {
    usize freed = 0;
    // Ordem de descarte: as categorias mais baratas de refazer primeiro.
    // Miniaturas custam um decode pequeno; frames decodificados custam um
    // decode grande; geometria custa um re-upload. Então: thumbnails, frames
    // compostos, frames decodificados, geometria, texturas.
    static constexpr MemoryClass kOrder[] = {
        MemoryClass::Thumbnails,
        MemoryClass::RenderedFrames,
        MemoryClass::DecodedFrames,
        MemoryClass::GpuGeometry,
        MemoryClass::GpuTextures,
        MemoryClass::Audio,
        MemoryClass::Proxies,
    };

    for (MemoryClass cls : kOrder) {
        if (freed >= bytes) break;
        for (u32 i = 0; i < reclaimableCount_ && freed < bytes; ++i) {
            IMemoryReclaimable* r = reclaimables_[i];
            if (!r || r->memory_class() != cls) continue;

            const usize got = r->reclaim(bytes - freed);
            // O contador da categoria é descontado AQUI, não pelo cache. O
            // contrato está em IMemoryReclaimable::reclaim — se os dois lados
            // descontassem, o contador deixaria de corresponder à memória real
            // e o orçamento passaria a liberar mais do que existe.
            if (got) free(cls, got);
            freed += got;
        }
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
    if (reclaimableCount_ >= kMaxReclaimables) return Errc::OutOfRange;
    for (u32 i = 0; i < reclaimableCount_; ++i) {
        if (reclaimables_[i] == r) return Errc::AlreadyExists;
    }
    reclaimables_[reclaimableCount_++] = r;
    return OkStatus;
}

void MemoryManager::unregister_reclaimable(IMemoryReclaimable* r) noexcept {
    for (u32 i = 0; i < reclaimableCount_; ++i) {
        if (reclaimables_[i] != r) continue;
        // Troca com o último: a ordem não importa, o descarte é por categoria.
        reclaimables_[i] = reclaimables_[reclaimableCount_ - 1];
        reclaimables_[reclaimableCount_ - 1] = nullptr;
        --reclaimableCount_;
        return;
    }
}

} // namespace aurea

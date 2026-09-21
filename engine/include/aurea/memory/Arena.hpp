// =============================================================================
//  Aurea / memory / Arena.hpp
//
//  Arena de bump allocation.
//
//  Por que existe: num editor, a maior parte das alocações tem vida curta e
//  escopo óbvio — o conjunto de parâmetros de um efeito durante a avaliação de
//  um frame, a lista de draws de um pass, os vértices de um texto triangulado.
//  `new`/`delete` para cada uma dessas custa mais do que o trabalho útil.
//
//  Regras: a arena NUNCA devolve memória individual; libera tudo de uma vez
//  (`reset()`). É por isso que ela é segura contra vazamento por construção.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <cstdlib>
#include <new>
#include <utility>

namespace aurea {

/// Arena de bump. Alinhamento padrão de 16 bytes (máximo que SIMD mobile usa).
class Arena {
public:
    static constexpr usize kDefaultBlockSize = 256 * 1024;   // 256 KiB
    static constexpr usize kDefaultAlignment = 16;

    Arena() = default;
    explicit Arena(usize blockSize) : blockSize_(blockSize ? blockSize : kDefaultBlockSize) {}
    ~Arena() { release(); }

    Arena(const Arena&)            = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&& o) noexcept { swap(o); }
    Arena& operator=(Arena&& o) noexcept { swap(o); return *this; }

    /// Troca o estado com outra arena. Os PONTEIROS já entregues continuam
    /// válidos: eles apontam para os blocos, e os blocos apenas trocam de dono.
    void swap(Arena& o) noexcept {
        u8* const b = block_;      block_ = o.block_;      o.block_ = b;
        const usize c = capacity_; capacity_ = o.capacity_; o.capacity_ = c;
        const usize off = offset_; offset_ = o.offset_;    o.offset_ = off;
        const usize u = used_;     used_ = o.used_;        o.used_ = u;
        const usize bs = blockSize_; blockSize_ = o.blockSize_; o.blockSize_ = bs;
        overflow_blocks_.swap(o.overflow_blocks_);
    }

    /// Aloca `size` bytes alinhados. Devolve nullptr em falta de memória — o
    /// motor não lança, então quem chama trata.
    [[nodiscard]] void* alloc(usize size, usize alignment = kDefaultAlignment) noexcept {
        const usize aligned = align_up(offset_, alignment);
        if (aligned + size > capacity_) {
            if (!grow(size + alignment)) return nullptr;
            return alloc(size, alignment);
        }
        void* p = block_ + aligned;
        offset_ = aligned + size;
        used_  += size;
        return p;
    }

    /// Aloca e constrói um objeto. O destrutor NÃO é chamado no reset — a arena
    /// é para tipos triviais ou tipos cujo estado não precisa de limpeza. Quem
    /// precisa de destrutor usa o objeto uma vez e chama `destroy()` antes.
    template <typename T, typename... Args>
    [[nodiscard]] T* make(Args&&... args) noexcept {
        void* p = alloc(sizeof(T), alignof(T));
        if (!p) return nullptr;
        return ::new (p) T(std::forward<Args>(args)...);
    }

    /// Aloca `count` elementos contíguos de `T`, sem construir.
    template <typename T>
    [[nodiscard]] T* alloc_array(usize count) noexcept {
        if (count == 0) return nullptr;
        return static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
    }

    /// Recomeça do zero. O bloco atual é reaproveitado — é o caso comum: a
    /// arena de frame é resetada 60 vezes por segundo e nunca cresce.
    void reset() noexcept {
        offset_ = 0;
        used_   = 0;
    }

    /// Devolve tudo ao sistema. Usado quando o editor fecha um projeto.
    void release() noexcept {
        if (block_) {
            // Blocos 16-alinhados vêm de malloc, então free é legítimo.
            std::free(block_);
            block_    = nullptr;
            capacity_ = 0;
            offset_   = 0;
            used_     = 0;
        }
        overflow_blocks_.clear_free();
    }

    [[nodiscard]] usize used() const noexcept { return used_; }
    [[nodiscard]] usize capacity() const noexcept { return capacity_; }

private:
    /// Blocos que cresceram além do bloco base. Guardados soltos para liberar.
    struct OverflowBlocks {
        void** items = nullptr;
        usize  count = 0;
        usize  cap   = 0;

        bool push(void* p) noexcept {
            if (count == cap) {
                const usize ncap = cap ? cap * 2 : 4;
                auto** n = static_cast<void**>(std::realloc(items, ncap * sizeof(void*)));
                if (!n) return false;
                items = n;
                cap   = ncap;
            }
            items[count++] = p;
            return true;
        }
        void clear_free() noexcept {
            for (usize i = 0; i < count; ++i) std::free(items[i]);
            std::free(items);
            items = nullptr;
            count = 0;
            cap   = 0;
        }

        void swap(OverflowBlocks& o) noexcept {
            void** const i = items; items = o.items; o.items = i;
            const usize c = count;  count = o.count; o.count = c;
            const usize k = cap;    cap   = o.cap;   o.cap   = k;
        }
    };

    [[nodiscard]] static constexpr usize align_up(usize v, usize a) noexcept {
        return (v + a - 1) & ~(a - 1);
    }

    bool grow(usize need) noexcept {
        const usize ncap = need > blockSize_ ? need : blockSize_;
        auto* nb = static_cast<u8*>(std::malloc(ncap));
        if (!nb) return false;
        // O bloco antigo passa a ser overflow: só é liberado no release().
        // Isso mantém ponteiros já entregues válidos até o reset, que é
        // exatamente o contrato que quem usa arena espera.
        if (block_ && !overflow_blocks_.push(block_)) {
            std::free(nb);
            return false;
        }
        block_    = nb;
        capacity_ = ncap;
        offset_   = 0;
        return true;
    }

    u8*   block_    = nullptr;
    usize capacity_ = 0;
    usize offset_   = 0;
    usize used_     = 0;
    usize blockSize_ = kDefaultBlockSize;
    OverflowBlocks overflow_blocks_{};
};

/// RAII: reseta a arena ao sair do escopo. É o padrão do frame:
///
///     { ArenaScope scope(frame_arena);  ... }
///     // aqui a arena já voltou ao início
class ArenaScope {
public:
    explicit ArenaScope(Arena& a) noexcept : arena_(a) {}
    ~ArenaScope() { arena_.reset(); }
    ArenaScope(const ArenaScope&)            = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;

private:
    Arena& arena_;
};

/// Vetor com armazenamento em arena. Cresce dobrando dentro da arena e nunca
/// realoca mais de uma vez por crescimento. Sem destrutor: só tipos triviais.
template <typename T>
class ArenaVector {
public:
    ArenaVector() = default;
    ArenaVector(Arena& arena, usize initial = 0) : arena_(&arena), cap_(initial) {
        if (initial) data_ = arena.alloc_array<T>(initial);
    }

    [[nodiscard]] bool push_back(const T& v) noexcept {
        if (size_ == cap_ && !grow()) return false;
        data_[size_++] = v;
        return true;
    }

    [[nodiscard]] T& operator[](usize i) noexcept { return data_[i]; }
    [[nodiscard]] const T& operator[](usize i) const noexcept { return data_[i]; }

    [[nodiscard]] T* begin() noexcept { return data_; }
    [[nodiscard]] T* end() noexcept { return data_ + size_; }
    [[nodiscard]] const T* begin() const noexcept { return data_; }
    [[nodiscard]] const T* end() const noexcept { return data_ + size_; }

    [[nodiscard]] usize size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] T* data() noexcept { return data_; }
    void clear() noexcept { size_ = 0; }

private:
    bool grow() noexcept {
        if (!arena_) return false;
        const usize ncap = cap_ ? cap_ * 2 : 16;
        T* nd = arena_->alloc_array<T>(ncap);
        if (!nd) return false;
        for (usize i = 0; i < size_; ++i) nd[i] = data_[i];
        data_ = nd;
        cap_  = ncap;
        return true;
    }

    Arena* arena_ = nullptr;
    T*     data_  = nullptr;
    usize  size_  = 0;
    usize  cap_   = 0;
};

} // namespace aurea

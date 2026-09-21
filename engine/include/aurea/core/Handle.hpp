// =============================================================================
//  Aurea / core / Handle.hpp
//
//  Handle de geração + tabela de slots.
//
//  Por que não ponteiro: a UI (Compose / SwiftUI) roda em outra thread, segura
//  referências a layers durante um gesto, e a timeline pode apagar essa layer
//  no meio do gesto. Um ponteiro viraria dangling sem avisar. Um handle carrega
//  a geração, então `resolve()` de um handle velho devolve nullptr de forma
//  determinística — e a UI trata isso como "a camada sumiu", não como crash.
//
//  A tabela é livre de fragmentação: slots vagos entram numa free-list
//  intrusiva (o índice livre fica no próprio campo `generation`), então
//  inserir/remover nunca realoca o vetor de slots.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <vector>
#include <cassert>

namespace aurea {

template <typename Tag>
struct Handle {
    u32 index      = kInvalidIndex;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalidIndex; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return valid(); }
    friend constexpr bool operator==(Handle, Handle) noexcept = default;

    /// Empacota em u64 para atravessar a bridge numa única palavra.
    [[nodiscard]] constexpr u64 pack() const noexcept {
        return (static_cast<u64>(generation) << 32) | static_cast<u64>(index);
    }
    [[nodiscard]] static constexpr Handle unpack(u64 v) noexcept {
        return Handle{static_cast<u32>(v & 0xFFFF'FFFFull),
                      static_cast<u32>(v >> 32)};
    }
};

// Tags de tipo — cada família de objeto tem seu próprio espaço de handles.
struct LayerTag;
struct CompositionTag;
struct AssetTag;
struct EffectTag;
struct MaskTag;
struct Scene3DTag;
struct ParticleSystemTag;
struct FontTag;
struct LightTag;
struct ModelInstanceTag;
struct CameraTag;

using LayerId           = Handle<LayerTag>;
using CompositionId     = Handle<CompositionTag>;
using AssetId           = Handle<AssetTag>;
using EffectId          = Handle<EffectTag>;
using MaskId            = Handle<MaskTag>;
using Scene3DId         = Handle<Scene3DTag>;
using ParticleSystemId  = Handle<ParticleSystemTag>;
using FontId            = Handle<FontTag>;
using LightId           = Handle<LightTag>;
using ModelInstanceId   = Handle<ModelInstanceTag>;
using CameraId          = Handle<CameraTag>;

inline constexpr u32 kInvalidHandleIndex = kInvalidIndex;

/// Tabela de slots com geração. `T` precisa ser movível e ter um estado vazio
/// barato (o padrão é `T{}`).
template <typename T, typename Tag>
class SlotTable {
public:
    using Id = Handle<Tag>;

    SlotTable() { slots_.reserve(64); }

    SlotTable(const SlotTable&)            = delete;
    SlotTable& operator=(const SlotTable&) = delete;
    SlotTable(SlotTable&&) noexcept        = default;
    SlotTable& operator=(SlotTable&&) noexcept = default;

    /// Reserva para `n` objetos vivos sem realocar. Chamar antes de um import
    /// grande evita pico de alocação no meio do playback.
    void reserve(u32 n) { slots_.reserve(n); }

    template <typename... Args>
    [[nodiscard]] Id create(Args&&... args) {
        u32 index;
        if (freeHead_ != kInvalidIndex) {
            index    = freeHead_;
            freeHead_ = slots_[index].nextFree;
        } else {
            index = static_cast<u32>(slots_.size());
            slots_.emplace_back();
        }
        Slot& s = slots_[index];
        s.object     = T(std::forward<Args>(args)...);
        s.alive      = true;
        s.nextFree   = kInvalidIndex;
        ++count_;
        return Id{index, s.generation};
    }

    [[nodiscard]] bool destroy(Id id) noexcept {
        if (!contains(id)) return false;
        Slot& s = slots_[id.index];
        s.object = T{};
        s.alive  = false;
        // Geração par: slot vivo. Ímpar: slot livre. Assim um handle antigo
        // nunca colide com o próximo ocupante do mesmo slot.
        ++s.generation;
        s.nextFree = freeHead_;
        freeHead_  = id.index;
        --count_;
        return true;
    }

    [[nodiscard]] T* get(Id id) noexcept {
        if (!contains(id)) return nullptr;
        return &slots_[id.index].object;
    }
    [[nodiscard]] const T* get(Id id) const noexcept {
        if (!contains(id)) return nullptr;
        return &slots_[id.index].object;
    }

    [[nodiscard]] bool contains(Id id) const noexcept {
        return id.index < slots_.size() && slots_[id.index].alive
            && slots_[id.index].generation == id.generation;
    }

    [[nodiscard]] u32 count() const noexcept { return count_; }

    /// Percorre todos os slots vivos na ordem de índice (ordem de criação).
    /// A ordem importa: é a ordem de composição vertical da timeline.
    template <typename Fn>
    void for_each(Fn&& fn) {
        for (u32 i = 0; i < slots_.size(); ++i) {
            if (slots_[i].alive) fn(Id{i, slots_[i].generation}, slots_[i].object);
        }
    }

    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (u32 i = 0; i < slots_.size(); ++i) {
            if (slots_[i].alive) fn(Id{i, slots_[i].generation}, slots_[i].object);
        }
    }

    void clear() noexcept {
        slots_.clear();
        freeHead_ = kInvalidIndex;
        count_    = 0;
    }

    /// Cópia profunda EXPLÍCITA (histórico de desfazer). A cópia implícita é
    /// proibida de propósito: copiar uma tabela de layers sem querer custaria
    /// caro e passaria despercebido. Gerações e lista livre vêm juntas, então
    /// os handles antigos continuam válidos na cópia.
    void copy_from(const SlotTable& other) {
        slots_ = other.slots_;
        freeHead_ = other.freeHead_;
        count_ = other.count_;
    }

private:
    struct Slot {
        T   object{};
        u32 generation = 1;            // ímpar = livre, par = vivo
        u32 nextFree   = kInvalidIndex;
        bool alive     = false;
    };

    std::vector<Slot> slots_;
    u32 freeHead_ = kInvalidIndex;
    u32 count_    = 0;
};

/// Índice estável para uma lista ordenada de ids, usado quando a UI precisa
/// reordenar (drag vertical na timeline) sem invalidar os handles.
template <typename Id>
class OrderedIds {
public:
    void push_back(Id id) { ids_.push_back(id); }

    [[nodiscard]] bool erase(Id id) {
        for (auto it = ids_.begin(); it != ids_.end(); ++it) {
            if (*it == id) { ids_.erase(it); return true; }
        }
        return false;
    }

    /// Move `id` para a posição `targetIndex`, empurrando o resto.
    bool move_to(Id id, u32 targetIndex) {
        const auto n = static_cast<u32>(ids_.size());
        if (targetIndex >= n) return false;
        for (u32 i = 0; i < n; ++i) {
            if (ids_[i] == id) {
                if (i == targetIndex) return true;
                Id tmp = ids_[i];
                ids_.erase(ids_.begin() + i);
                ids_.insert(ids_.begin() + targetIndex, tmp);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] i32 index_of(Id id) const noexcept {
        for (u32 i = 0; i < ids_.size(); ++i) if (ids_[i] == id) return static_cast<i32>(i);
        return -1;
    }

    [[nodiscard]] u32 size() const noexcept { return static_cast<u32>(ids_.size()); }
    [[nodiscard]] bool empty() const noexcept { return ids_.empty(); }
    [[nodiscard]] Id at(u32 i) const noexcept { return ids_[i]; }
    [[nodiscard]] Id& at(u32 i) noexcept { return ids_[i]; }
    [[nodiscard]] const std::vector<Id>& raw() const noexcept { return ids_; }
    void clear() noexcept { ids_.clear(); }

private:
    std::vector<Id> ids_;
};

} // namespace aurea

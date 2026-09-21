// =============================================================================
//  Aurea / memory / MemoryManager.hpp
//
//  Orçamento de memória do editor.
//
//  O ponto de partida: celular NÃO tem memória infinita, e o Android mata o
//  processo em background sem aviso quando ele passa do limite. Um editor que
//  guarda cache de frames decodificados "porque dá" é morto no meio de um
//  projeto de 20 minutos.
//
//  Então o motor não pergunta "cabe?". Ele mantém um ORÇAMENTO por categoria e
//  um contador de pressão. Quando `pressure()` sobe, quem tem cache descarta
//  pela ordem definida em `eviction_order`. É isso que faz o Aurea aguentar
//  4K num aparelho médio em vez de morrer.
//
//  O teto de cada categoria vem de DeviceCapabilities (memória real do
//  aparelho), não de uma constante escolhida no escuro.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Result.hpp"

#include <atomic>

namespace aurea {

/// Categorias de memória. A ordem é a de descarte: o que está em cima é o
/// primeiro a ser liberado sob pressão.
enum class MemoryClass : u8 {
    Thumbnails = 0,   ///< cache de miniaturas da timeline
    Proxies,          ///< arquivos de proxy em disco (contabiliza em disco)
    DecodedFrames,    ///< frames decodificados aguardando composição
    RenderedFrames,   ///< frames compostos aguardando exibição
    GpuTextures,      ///< texturas residentes
    GpuGeometry,      ///< VBO/IBO, mesh, estrutura de aceleração
    Audio,            ///< buffers de áudio e waveforms
    Assets,           ///< fontes, shaders compilados, dados de projeto
    Persistent,       ///< o projeto aberto — NUNCA descartado sob pressão
    _Count,
};

[[nodiscard]] constexpr const char* to_string(MemoryClass c) noexcept {
    switch (c) {
        case MemoryClass::Thumbnails:    return "thumbnails";
        case MemoryClass::Proxies:       return "proxies";
        case MemoryClass::DecodedFrames: return "frames-decodificados";
        case MemoryClass::RenderedFrames:return "frames-compostos";
        case MemoryClass::GpuTextures:   return "texturas-gpu";
        case MemoryClass::GpuGeometry:   return "geometria-gpu";
        case MemoryClass::Audio:         return "audio";
        case MemoryClass::Assets:        return "assets";
        case MemoryClass::Persistent:    return "projeto";
        case MemoryClass::_Count:        break;
    }
    return "?";
}

/// Reserva de memória com escopo. Enquanto viva, o valor está contado no
/// orçamento da categoria. Soltar a reserva devolve a memória.
///
/// Uso: quem aloca uma textura cria uma Reservation junto dela. Se a alocação
/// falhar por orçamento, a reserva não foi concedida e a textura nem é criada —
/// melhor recusar uma textura do que aceitar e ser morto pelo sistema depois.
class MemoryManager;

class Reservation {
public:
    Reservation() = default;
    Reservation(MemoryManager* mgr, MemoryClass cls, usize bytes) noexcept
        : mgr_(mgr), cls_(cls), bytes_(bytes) {}
    ~Reservation() { release(); }

    Reservation(const Reservation&)            = delete;
    Reservation& operator=(const Reservation&) = delete;

    Reservation(Reservation&& o) noexcept
        : mgr_(o.mgr_), cls_(o.cls_), bytes_(o.bytes_) { o.mgr_ = nullptr; o.bytes_ = 0; }
    Reservation& operator=(Reservation&& o) noexcept {
        if (this != &o) { release(); mgr_ = o.mgr_; cls_ = o.cls_; bytes_ = o.bytes_; o.mgr_ = nullptr; o.bytes_ = 0; }
        return *this;
    }

    void release() noexcept;
    void resize(usize newBytes) noexcept;

    [[nodiscard]] bool valid() const noexcept { return mgr_ != nullptr; }
    [[nodiscard]] usize bytes() const noexcept { return bytes_; }

private:
    MemoryManager* mgr_ = nullptr;
    MemoryClass    cls_ = MemoryClass::Assets;
    usize          bytes_ = 0;
};

/// Interface de quem guarda memória descartável. Cada cache do motor
/// (DecodedFrameCache, ThumbnailCache, ...) implementa isto e se registra.
class IMemoryReclaimable {
public:
    virtual ~IMemoryReclaimable() = default;
    [[nodiscard]] virtual MemoryClass memory_class() const noexcept = 0;
    /// Libera até `targetBytes` e devolve quantos bytes desta categoria foram
    /// liberados de fato.
    ///
    /// CONTRATO: quem chama (o MemoryManager) desconta o valor devolvido do
    /// contador da categoria. O cache NÃO deve chamar `free()` por conta
    /// própria — fazer os dois lados descontarem transformaria o contador num
    /// número que não corresponde a nada.
    ///
    /// Devolver zero é legítimo (não havia nada para liberar) e não é erro.
    /// Deve ser seguro chamar de outra thread.
    [[nodiscard]] virtual usize reclaim(usize targetBytes) noexcept = 0;
    [[nodiscard]] virtual const char* debug_name() const noexcept = 0;
};

class MemoryManager {
public:
    static constexpr u32 kMaxReclaimables = 32;

    MemoryManager() = default;

    /// Define o teto de cada categoria. Chamado uma vez, na inicialização, com
    /// os números medidos em DeviceCapabilities.
    void set_budget(MemoryClass cls, usize bytes) noexcept {
        budgets_[index(cls)] = bytes;
    }
    [[nodiscard]] usize budget(MemoryClass cls) const noexcept { return budgets_[index(cls)]; }
    [[nodiscard]] usize used(MemoryClass cls) const noexcept { return used_[index(cls)].load(std::memory_order_relaxed); }

    [[nodiscard]] usize total_budget() const noexcept {
        usize t = 0;
        for (u8 i = 0; i < kClassCount; ++i) t += budgets_[i];
        return t;
    }
    [[nodiscard]] usize total_used() const noexcept {
        usize t = 0;
        for (u8 i = 0; i < kClassCount; ++i) t += used_[i].load(std::memory_order_relaxed);
        return t;
    }

    /// Tenta reservar. Devolve reserva inválida se estouraria o orçamento da
    /// categoria E os reclaimables daquela categoria não conseguiram liberar o
    /// suficiente.
    ///
    /// `Persistent` nunca é recusado: o projeto aberto não pode falhar por
    /// orçamento. Se ele não couber, o problema é o teto, não a alocação.
    [[nodiscard]] Reservation try_reserve(MemoryClass cls, usize bytes) noexcept;

    /// Reserva que pode invadir outras categorias. Usada só por caminhos que
    /// não podem falhar (buffer do compositor do frame atual).
    [[nodiscard]] Reservation reserve_critical(MemoryClass cls, usize bytes) noexcept;

    void commit(MemoryClass cls, usize bytes) noexcept;
    void free(MemoryClass cls, usize bytes) noexcept;

    /// Fração do orçamento total em uso, 0..1.
    [[nodiscard]] f32 pressure() const noexcept {
        const usize b = total_budget();
        if (b == 0) return 0.0f;
        return static_cast<f32>(static_cast<f64>(total_used()) / static_cast<f64>(b));
    }

    /// Pede a todos os caches registrados que liberem `bytes`. Devolve o total
    /// efetivamente liberado.
    usize request_reclaim(usize bytes) noexcept;

    /// Registra um cache. `Persistent` é ignorado — não é descartável.
    [[nodiscard]] Status register_reclaimable(IMemoryReclaimable* r) noexcept;
    void unregister_reclaimable(IMemoryReclaimable* r) noexcept;

    /// Contagem de vezes que uma recusa aconteceu. É o número que o painel de
    /// telemetria mostra: se cresce durante playback, o orçamento está apertado.
    [[nodiscard]] u64 rejection_count() const noexcept { return rejections_.load(std::memory_order_relaxed); }

private:
    static constexpr u8 kClassCount = static_cast<u8>(MemoryClass::_Count);
    [[nodiscard]] static constexpr u8 index(MemoryClass c) noexcept { return static_cast<u8>(c); }

    std::atomic<usize> used_[kClassCount]{};
    usize              budgets_[kClassCount]{};
    IMemoryReclaimable* reclaimables_[kMaxReclaimables]{};
    u32                reclaimableCount_ = 0;
    std::atomic<u64>   rejections_{0};
};

} // namespace aurea

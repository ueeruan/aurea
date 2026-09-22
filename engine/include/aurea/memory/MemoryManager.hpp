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
//  aparelho), não de uma constante escolhida no escuro. A tabela de divisão
//  mora em `kBudgetShare` (abaixo) e está documentada em
//  docs/performance/PHASE_8_REPORT.md §8B.
//
//  Fase 8 (§13–15):
//   - pressão do SISTEMA (`trim`): o nível que o Android manda em
//     onTrimMemory vira um estágio da ordem de despejo do spec
//     (miniaturas fora da tela → waveform antiga → quadros sem uso → cache de
//     render antigo → mips altos → assets 3D sem uso → temporários). O projeto
//     (Persistent) nunca entra: estado, alterações e timeline não se perdem;
//   - todo cache registrado publica métricas (bytes, orçamento, entradas,
//     acertos, erros, despejos, versão) — `collect_metrics`;
//   - o registro é protegido por mutex: fontes de vídeo nascem e morrem na
//     thread de render enquanto a UI pode estar pedindo um trim.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Result.hpp"

#include <atomic>
#include <mutex>

/// Marca de API (testes de medição compilam contra a versão antiga e a nova).
#define AUREA_MEMORY_API_8B 1

namespace aurea {

/// Categorias de memória. A ordem é a de descarte: o que está em cima é o
/// primeiro a ser liberado sob pressão.
enum class MemoryClass : u8 {
    Thumbnails = 0,   ///< cache de miniaturas da timeline
    Waveforms,        ///< picos de áudio da timeline (multirresolução)
    Proxies,          ///< arquivos de proxy em disco (contabiliza em disco)
    DecodedFrames,    ///< frames decodificados aguardando composição
    RenderedFrames,   ///< cache de render: flow, máscara, planos, LUT, pool transitório
    GpuTextures,      ///< texturas residentes (imagens, texturas 3D)
    GpuGeometry,      ///< VBO/IBO, mesh, partículas, malha vetorial
    Audio,            ///< blocos de áudio decodificados
    Assets,           ///< fontes, atlas de glifos, shaders compilados
    Export,           ///< buffers do export (readback, fila do encoder)
    Persistent,       ///< o projeto aberto — NUNCA descartado sob pressão
    _Count,
};

[[nodiscard]] constexpr const char* to_string(MemoryClass c) noexcept {
    switch (c) {
        case MemoryClass::Thumbnails:    return "thumbnails";
        case MemoryClass::Waveforms:     return "waveforms";
        case MemoryClass::Proxies:       return "proxies";
        case MemoryClass::DecodedFrames: return "frames-decodificados";
        case MemoryClass::RenderedFrames:return "cache-de-render";
        case MemoryClass::GpuTextures:   return "texturas-gpu";
        case MemoryClass::GpuGeometry:   return "geometria-gpu";
        case MemoryClass::Audio:         return "audio";
        case MemoryClass::Assets:        return "assets";
        case MemoryClass::Export:        return "export";
        case MemoryClass::Persistent:    return "projeto";
        case MemoryClass::_Count:        break;
    }
    return "?";
}

/// Divisão do orçamento total entre as categorias, em milésimos (soma 1000).
/// Um número por consumidor; o motor aplica em `Engine::apply_memory_budgets`.
inline constexpr u16 kBudgetShare[static_cast<u8>(MemoryClass::_Count)] = {
    15,    // Thumbnails (atrás do cache de bitmaps da UI: acerto medido 0%, só faz a ponte até a UI buscar)
    10,    // Waveforms
    80,    // Proxies
    265,   // DecodedFrames
    140,   // RenderedFrames
    200,   // GpuTextures
    100,   // GpuGeometry
    40,    // Audio
    60,    // Assets
    70,    // Export
    20,    // Persistent
};

/// Estágios da ordem de despejo sob pressão do sistema (§13). Cada cache diz
/// em qual estágio ele entra; `trim(estágio)` roda do 1 até o pedido.
enum class TrimStage : u8 {
    None = 0,
    OffscreenThumbnails = 1,   ///< miniaturas fora da tela
    OldWaveforms,              ///< waveform que ninguém consulta
    UnusedDecodedFrames,       ///< quadros decodificados sem uso (e blocos de áudio parados)
    OldRenderCache,            ///< cache de render antigo (flow, máscara, planos, pool)
    HighMips,                  ///< mips altos
    Unused3DAssets,            ///< assets 3D sem uso
    Temporaries,               ///< temporários
    _Count,
};

[[nodiscard]] constexpr const char* to_string(TrimStage s) noexcept {
    switch (s) {
        case TrimStage::None:                return "nenhum";
        case TrimStage::OffscreenThumbnails: return "miniaturas-fora-da-tela";
        case TrimStage::OldWaveforms:        return "waveform-antiga";
        case TrimStage::UnusedDecodedFrames: return "quadros-sem-uso";
        case TrimStage::OldRenderCache:      return "cache-de-render-antigo";
        case TrimStage::HighMips:            return "mips-altos";
        case TrimStage::Unused3DAssets:      return "assets-3d-sem-uso";
        case TrimStage::Temporaries:         return "temporarios";
        case TrimStage::_Count:              break;
    }
    return "?";
}

/// Nível do Android (ComponentCallbacks2.TRIM_MEMORY_*) → até que estágio
/// despejar. Valores do SDK: RUNNING_MODERATE 5, RUNNING_LOW 10,
/// RUNNING_CRITICAL 15, UI_HIDDEN 20, BACKGROUND 40, MODERATE 60, COMPLETE 80.
/// iOS (didReceiveMemoryWarning) usa o de RUNNING_CRITICAL.
[[nodiscard]] constexpr TrimStage trim_stage_for_os_level(i32 level) noexcept {
    if (level >= 80) return TrimStage::Temporaries;          // COMPLETE: o próximo a morrer somos nós
    if (level >= 60) return TrimStage::Unused3DAssets;       // MODERATE
    if (level >= 40) return TrimStage::OldRenderCache;       // BACKGROUND
    if (level >= 20) return TrimStage::UnusedDecodedFrames;  // UI_HIDDEN: nada na tela
    if (level >= 15) return TrimStage::OldRenderCache;       // RUNNING_CRITICAL
    if (level >= 10) return TrimStage::UnusedDecodedFrames;  // RUNNING_LOW
    if (level >= 5)  return TrimStage::OldWaveforms;         // RUNNING_MODERATE
    return TrimStage::None;
}

/// Métricas de um cache (§15). Tudo que o painel de telemetria e o relatório
/// mostram sai daqui.
struct CacheMetrics {
    const char* name = "";
    MemoryClass cls = MemoryClass::Assets;
    TrimStage   stage = TrimStage::None;
    u64 bytes = 0;
    u64 budgetBytes = 0;
    u32 entries = 0;
    u64 hits = 0;
    u64 misses = 0;
    u64 evictions = 0;
    u32 version = 0;      ///< muda a cada invalidação (conteúdo antigo não vale mais)
    [[nodiscard]] f32 hit_rate() const noexcept {
        const u64 n = hits + misses;
        return n ? static_cast<f32>(static_cast<f64>(hits) / static_cast<f64>(n)) : 0.0f;
    }
};

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
/// (DecodedFrameCache, ThumbnailService, WaveformCache, ...) implementa isto
/// e se registra.
class IMemoryReclaimable {
public:
    virtual ~IMemoryReclaimable() = default;
    [[nodiscard]] virtual MemoryClass memory_class() const noexcept = 0;
    /// Libera até `targetBytes` e devolve quantos bytes desta categoria foram
    /// liberados de fato. `targetBytes == kReclaimAll` é o pedido do trim:
    /// tudo o que está SEM USO (o que está na tela / tocando fica).
    ///
    /// CONTRATO DE CONTAGEM: por padrão quem chama (o MemoryManager) desconta o
    /// valor devolvido do contador da categoria e o cache NÃO chama `free()`.
    /// Um cache que conta os próprios bytes (commit/free a cada inserção e
    /// despejo) devolve `accounts_itself() == true`, e aí o gerenciador não
    /// desconta nada — descontar dos dois lados faria o contador deixar de
    /// corresponder à memória real.
    ///
    /// Devolver zero é legítimo (não havia nada para liberar) e não é erro.
    /// Deve ser seguro chamar de outra thread.
    [[nodiscard]] virtual usize reclaim(usize targetBytes) noexcept = 0;
    [[nodiscard]] virtual const char* debug_name() const noexcept = 0;

    /// Em que estágio da pressão do sistema este cache entra.
    [[nodiscard]] virtual TrimStage trim_stage() const noexcept;
    [[nodiscard]] virtual bool accounts_itself() const noexcept { return false; }
    /// Preenche as métricas. false = o cache não publica métricas.
    [[nodiscard]] virtual bool metrics(CacheMetrics& out) const noexcept { (void)out; return false; }
};

class MemoryManager {
public:
    static constexpr u32 kMaxReclaimables = 64;
    static constexpr usize kReclaimAll = ~static_cast<usize>(0);

    MemoryManager() = default;

    /// Define o teto de cada categoria. Chamado na inicialização (e de novo
    /// quando a GPU real é medida) com os números de DeviceCapabilities.
    void set_budget(MemoryClass cls, usize bytes) noexcept {
        budgets_[index(cls)].store(bytes, std::memory_order_relaxed);
    }
    /// Divide `totalBytes` pela tabela `kBudgetShare`.
    void apply_budget_table(u64 totalBytes) noexcept;
    [[nodiscard]] usize budget(MemoryClass cls) const noexcept { return budgets_[index(cls)].load(std::memory_order_relaxed); }
    [[nodiscard]] usize used(MemoryClass cls) const noexcept { return used_[index(cls)].load(std::memory_order_relaxed); }
    /// Maior valor de `used` já visto na categoria (pico da sessão).
    [[nodiscard]] usize peak(MemoryClass cls) const noexcept { return peak_[index(cls)].load(std::memory_order_relaxed); }

    [[nodiscard]] usize total_budget() const noexcept {
        usize t = 0;
        for (u8 i = 0; i < kClassCount; ++i) t += budgets_[i].load(std::memory_order_relaxed);
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

    /// Pede aos caches registrados que liberem `bytes`, na ordem de descarte
    /// das categorias. Devolve o total efetivamente liberado.
    usize request_reclaim(usize bytes) noexcept;
    /// Pede só aos caches da categoria `cls`.
    usize reclaim_class(MemoryClass cls, usize bytes) noexcept;

    /// Resultado de um trim: bytes liberados por estágio.
    struct TrimReport {
        TrimStage upTo = TrimStage::None;
        u64 freed[static_cast<u8>(TrimStage::_Count)]{};
        u64 total = 0;
    };
    /// Pressão do sistema: roda os estágios 1..`upTo` em ordem, pedindo a
    /// cada cache daquele estágio que solte tudo o que está sem uso. Os
    /// estágios que não moram em caches registrados (cache de render, mips,
    /// assets 3D, temporários) são do dono deles — o motor os roda em
    /// `Engine::trim_memory` e soma `note_trim_freed`.
    TrimReport trim(TrimStage upTo) noexcept;
    /// Soma ao relatório do último trim o que um dono de fora liberou.
    void note_trim_freed(TrimStage stage, u64 bytes) noexcept;
    [[nodiscard]] TrimReport last_trim() const noexcept;
    [[nodiscard]] u64 trim_count() const noexcept { return trims_.load(std::memory_order_relaxed); }

    /// Categorias acima do orçamento pedem despejo aos próprios caches.
    /// Barato quando nada estourou; o motor chama a cada ~2 s.
    usize balance() noexcept;

    /// Registra um cache. `Persistent` é ignorado — não é descartável.
    [[nodiscard]] Status register_reclaimable(IMemoryReclaimable* r) noexcept;
    void unregister_reclaimable(IMemoryReclaimable* r) noexcept;
    [[nodiscard]] u32 reclaimable_count() const noexcept;

    /// Métricas de todos os caches registrados (até `capacity`). Devolve
    /// quantas foram escritas.
    u32 collect_metrics(CacheMetrics* out, u32 capacity) const noexcept;

    /// Contagem de vezes que uma recusa aconteceu. É o número que o painel de
    /// telemetria mostra: se cresce durante playback, o orçamento está apertado.
    [[nodiscard]] u64 rejection_count() const noexcept { return rejections_.load(std::memory_order_relaxed); }

private:
    static constexpr u8 kClassCount = static_cast<u8>(MemoryClass::_Count);
    [[nodiscard]] static constexpr u8 index(MemoryClass c) noexcept { return static_cast<u8>(c); }
    usize reclaim_locked(MemoryClass cls, usize bytes) noexcept;

    std::atomic<usize> used_[kClassCount]{};
    std::atomic<usize> peak_[kClassCount]{};
    std::atomic<usize> budgets_[kClassCount]{};
    mutable std::mutex  registryMutex_;
    IMemoryReclaimable* reclaimables_[kMaxReclaimables]{};
    u32                reclaimableCount_ = 0;
    std::atomic<u64>   rejections_{0};
    std::atomic<u64>   trims_{0};
    mutable std::mutex trimMutex_;
    TrimReport         lastTrim_{};
};

} // namespace aurea

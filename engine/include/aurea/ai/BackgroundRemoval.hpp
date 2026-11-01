#pragma once

// =============================================================================
//  Aurea / ai / BackgroundRemoval.hpp
//
//  "Remover fundo" (aurea.key.background_remove): o fundo de uma camada de
//  vídeo/imagem sai em todos os quadros, sem tela verde (referência de
//  comportamento: o removedor de fundo de vídeo dos editores móveis).
//
//   - Probabilidade por quadro da FONTE, na grade 320² do quadro inteiro:
//     o U2Net-P embutido (o mesmo do Rotobrush IA, offline, as duas
//     plataformas) ou, no modelo "Pessoa", um segmentador NATIVO registrado
//     pela plataforma (Vision no iOS). Sem o nativo, vale o embutido.
//   - Estabilidade no tempo: mediana de 3 quadros compensada por blocos e
//     com rejeição de corte (`stabilize_foreground`), depois a borda encosta
//     nas arestas do próprio quadro (segmentação geodésica do Roto Brush).
//     Janela simétrica e finita: preview, seek, ordem do export e reabrir o
//     projeto dão o MESMO recorte.
//   - Cache por quadro (`BgMatteStore`): RLE de 8 bits, compartilhado entre o
//     renderizador da prévia e o do export, salvo ao lado do projeto
//     (`<projeto>.bgmask`) — "Analisar clipe" sobrevive a salvar/reabrir.
//   - Borda fina (RotoRefine, raio > 0): a faixa da borda decidida de novo na
//     resolução do quadro; só em memória (refaz do recorte salvo).
//   - O resto do refino (limite, suavidade, contrair/expandir, contraste,
//     suavizar, inverter, descontaminar, mistura, visualização) é do shader
//     do Rotobrush: muda na hora, sem recalcular nada.
// =============================================================================

#include "aurea/ai/DepthMapService.hpp"
#include "aurea/ai/RotoRefine.hpp"
#include "aurea/core/Result.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace aurea::ai {

inline constexpr u32 kBgMatteSize = ForegroundEstimator::kSize;
/// Muda quando o recorte calculado muda: o salvo com outra versão não vale.
inline constexpr u32 kBgAlgorithmVersion = 1;
/// Modelos (parâmetro "model" do efeito; valores gravados nos projetos).
inline constexpr u32 kBgModelAuto = 0;     ///< objeto em destaque (U2Net-P embutido)
inline constexpr u32 kBgModelPerson = 1;   ///< pessoa (segmentador nativo; sem ele, o embutido)
/// Lado maior do quadro entregue ao segmentador nativo.
inline constexpr u32 kBgNativeSide = 640;

// --- Segmentador nativo (opcional, por plataforma) ----------------------------
/// `rgb` RGB8 sem preenchimento (w×h). Escreve a probabilidade de PRIMEIRO
/// PLANO (0..1) em `out`, `outSize`², cobrindo o quadro inteiro (a mesma grade
/// do U2Net). false = indisponível ou falhou (vale o embutido). Chamado no
/// worker do Remover fundo (nunca na thread da interface).
using BgNativeSegmenter = bool (*)(void* ctx, u32 model, const u8* rgb, u32 width, u32 height, u32 outSize, f32* out);
void bg_set_native_segmenter(BgNativeSegmenter fn, void* ctx) noexcept;
[[nodiscard]] bool bg_native_available(u32 model = kBgModelPerson) noexcept;
/// O modelo que vale de fato (Pessoa sem nativo = Automático).
[[nodiscard]] u32 bg_effective_model(u32 requested) noexcept;

// --- O recorte de um quadro ---------------------------------------------------
/// Um quadro cru: probabilidade e guia de cor, ambas kBgMatteSize².
struct BgRaw {
    std::vector<f32> prob;
    std::vector<u8> rgb;
    [[nodiscard]] bool valid() const noexcept {
        const usize n = static_cast<usize>(kBgMatteSize) * kBgMatteSize;
        return prob.size() == n && rgb.size() == n * 3;
    }
};
using BgRawPtr = std::shared_ptr<const BgRaw>;

/// Recorte final de `cur` (0..1, kBgMatteSize²). `prev`/`next` nulos ou
/// inválidos = borda do clipe (vale só o atual). Determinístico.
void bg_matte(const BgRaw* prev, const BgRaw& cur, const BgRaw* next, std::vector<f32>& out);

/// Chave do cache/arquivo: conteúdo da fonte + modelo + versão do algoritmo.
/// Usa o hash do conteúdo quando existe (o caminho muda entre aparelhos).
[[nodiscard]] u64 bg_matte_key(u64 contentHash, const std::string& sourcePath, u32 width, u32 height, i64 frameCount,
                               u32 model) noexcept;

// --- Cache persistente --------------------------------------------------------
class BgMatteStore {
public:
    static constexpr usize kMaxBytes = 128u * 1024u * 1024u;
    static constexpr usize kLowMemoryMaxBytes = 48u * 1024u * 1024u;

    BgMatteStore() = default;
    BgMatteStore(const BgMatteStore&) = delete;
    BgMatteStore& operator=(const BgMatteStore&) = delete;

    [[nodiscard]] bool get(u64 key, i64 frame, std::vector<f32>& out);
    [[nodiscard]] bool has(u64 key, i64 frame);
    /// Quantos quadros de [first, last] estão prontos.
    [[nodiscard]] i64 count(u64 key, i64 first, i64 last);
    void put(u64 key, i64 frame, const std::vector<f32>& matte);
    /// Tira todos os quadros de uma fonte (recalcular).
    void erase(u64 key);
    /// Mantém só as chaves dadas (o resto era de mídia que saiu do projeto).
    void retain(const std::vector<u64>& keys);
    [[nodiscard]] usize bytes();
    [[nodiscard]] usize frames();
    void set_max_bytes(usize bytes) noexcept;
    [[nodiscard]] bool dirty() const noexcept { return dirty_.load(std::memory_order_relaxed); }

    /// Formato do arquivo: "AUBGMSK1", versão, grade, faixas (chave, quadros:
    /// índice + RLE) e FNV-1a de tudo no fim. Arquivo truncado/corrompido é
    /// recusado inteiro (nunca um recorte errado).
    void encode(std::vector<u8>& out);
    /// Junta o conteúdo do arquivo ao cache (o que já está em memória vence).
    [[nodiscard]] bool decode(const u8* data, usize size);

    /// Projeto aberto/novo: esquece tudo e passa a usar `path` (vazio = sem
    /// arquivo). A leitura é preguiçosa: só quando alguém pede um recorte.
    void attach(std::string path);
    [[nodiscard]] std::string path();
    /// Grava se mudou (atômico). Sem caminho: nada. `keys` não vazio: poda antes.
    Status flush(const std::vector<u64>* keepKeys = nullptr);
    /// Grava em outro caminho (Salvar como) e passa a usar ele.
    Status flush_to(const std::string& path, const std::vector<u64>* keepKeys = nullptr);
    /// Pressão de memória: com arquivo, grava o que mudou e solta a memória
    /// (relê sob demanda). Sem arquivo, nada sai (seria trabalho perdido).
    usize release_memory();

private:
    struct Track { std::unordered_map<i64, std::vector<u8>> frames; u64 stamp = 0; };
    void ensure_loaded_locked();
    void evict_locked(u64 keepKey, i64 keepFrame);
    std::mutex mutex_;
    std::unordered_map<u64, Track> tracks_;
    usize bytes_ = 0;
    u64 stamp_ = 0;
    usize maxBytes_ = kMaxBytes;
    std::string path_;
    bool loaded_ = true;
    std::atomic<bool> dirty_{false};
};
[[nodiscard]] std::shared_ptr<BgMatteStore> make_bg_matte_store();

// --- Serviço (worker, progresso, cancelamento) --------------------------------
class BgRemoveService {
public:
    using MattePtr = std::shared_ptr<const std::vector<f32>>;
    using RefinedPtr = std::shared_ptr<const RotoRefined>;
    static constexpr u32 kSize = kBgMatteSize;
    static constexpr usize kRefinedMaxBytes = 48u * 1024u * 1024u;
    static constexpr usize kLowMemoryRefinedMaxBytes = 16u * 1024u * 1024u;
    static constexpr usize kRawKept = 12;

    /// Um vídeo (cópia: o modelo pode mudar durante o cálculo).
    struct Source {
        DepthMapService* fg = nullptr;           ///< U2Net (modo primeiro plano) + decoder
        VideoSourceFactory* factory = nullptr;
        Asset asset;
        u64 rawKey = 0;        ///< namespace dos mapas crus do `fg` (o mesmo do Rotobrush)
        u64 matteKey = 0;      ///< chave do BgMatteStore
        u32 model = kBgModelAuto;   ///< modelo EFETIVO
        f64 fps = 30.0;
        i64 frameCount = 1;
        f32 layerW = 1.0f, layerH = 1.0f;
        RotoRefineSettings refine;   ///< raio em px da FONTE
        u32 refineMaxSide = kRotoRefineMaxSide;
    };
    struct Result { MattePtr matte; RefinedPtr refined; };
    struct Progress {
        u64 matteKey = 0;
        i64 done = 0, total = 0;
        bool running = false, failed = false;
        u32 reason = 0;        ///< DepthMapService::Failure
    };

    explicit BgRemoveService(std::shared_ptr<BgMatteStore> store);
    ~BgRemoveService();
    BgRemoveService(const BgRemoveService&) = delete;
    BgRemoveService& operator=(const BgRemoveService&) = delete;

    [[nodiscard]] const std::shared_ptr<BgMatteStore>& store() const noexcept { return store_; }
    [[nodiscard]] MattePtr cached(u64 matteKey, i64 frame);
    [[nodiscard]] RefinedPtr cached_refined(u64 matteKey, i64 frame, u64 refineKey);
    void put_refined(u64 matteKey, i64 frame, u64 refineKey, const RotoRefined& refined);
    /// O recorte (e o refinado, com refino) de `frame`, calculado aqui (export).
    [[nodiscard]] Result compute(const Source& source, i64 frame, u64 deadlineNs = 0);
    /// Preview: agenda um quadro (o pedido mais novo vence; durante a análise,
    /// vai na frente dela).
    void request(const Source& source, i64 frame);
    /// "Analisar clipe": [first, last] no worker, com progresso.
    void analyze(const Source& source, i64 first, i64 last);
    [[nodiscard]] Progress progress() const;
    void cancel() noexcept;
    void set_ready_callback(void (*fn)(void*), void* ctx) noexcept;
    void set_low_memory(bool low) noexcept;
    /// Projeto fechado: worker, fila e refinados fora (o store é de quem o deu).
    void clear() noexcept;
    /// Pressão de memória: refinados e quadros crus saem (o worker segue).
    void trim() noexcept;
    [[nodiscard]] u64 computed_frames() const noexcept { return computed_.load(std::memory_order_relaxed); }

    /// Quadro cru de um vídeo (rede/nativo + guia), com cache curto.
    [[nodiscard]] BgRawPtr raw(const Source& source, i64 frame);
    /// Quadro cru de uma imagem RGBA já lida (nativo) — o embutido vem do `fg`.
    [[nodiscard]] static BgRawPtr raw_from_native(u32 model, const RotoImage& image);

private:
    struct Job { Source source; i64 first = 0, last = 0; bool range = false; };
    struct RefinedEntry { u64 refineKey = 0; std::vector<u8> packed; u64 stamp = 0; };
    Result compute_impl(const Source& source, i64 frame, u64 deadlineNs, const std::atomic<u64>* generation, u64 expected);
    void thread_main() noexcept;
    void ensure_thread();
    void notify();

    std::shared_ptr<BgMatteStore> store_;
    mutable std::mutex mutex_;   ///< refinados + crus
    std::unordered_map<u64, RefinedEntry> refined_;
    usize refinedBytes_ = 0, refinedMax_ = kRefinedMaxBytes;
    struct RawSlot { u64 key = 0; BgRawPtr raw; u64 stamp = 0; };
    std::vector<RawSlot> raws_;
    u64 stamp_ = 0;

    mutable std::mutex queueMutex_;
    std::condition_variable wake_;
    std::unique_ptr<Job> pending_, focus_;
    bool running_ = false;
    std::thread thread_;
    std::atomic<u64> generation_{0};
    std::atomic<u64> computed_{0};
    std::atomic<u32> lastFailure_{0};
    Progress progress_{};
    void (*readyFn_)(void*) = nullptr;
    void* readyCtx_ = nullptr;
};

} // namespace aurea::ai

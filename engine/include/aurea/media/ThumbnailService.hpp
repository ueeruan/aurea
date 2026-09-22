// =============================================================================
//  Aurea / media / ThumbnailService.hpp
//
//  Miniaturas da timeline: pequenas (altura fixa, RGBA8 sRGB), aproximadas
//  (o quadro-chave mais próximo — a timeline precisa de "onde estou", não do
//  frame exato) e SEMPRE em prioridade baixa: uma thread própria, decoder
//  próprio em modo CPU, nunca disputando o decoder do preview.
//
//  A UI pede; se não está em cache, o pedido entra na fila (os mais recentes
//  primeiro — quem rola a timeline quer o que está na tela agora) e a
//  `generation()` muda quando algo fica pronto.
//
//  Fase 8 (§12, §14–15): o cache é por BYTES (categoria Thumbnails do
//  orçamento), LRU, com métricas (acertos/erros/despejos) e versão. A chave
//  leva o caminho da mídia: relinkar o asset invalida sozinho as miniaturas
//  velhas. Sob pressão do sistema (primeiro estágio) solta tudo — a UI guarda
//  os bitmaps do que está na tela.
// =============================================================================
#pragma once

#include "aurea/media/MediaManager.hpp"

#include <condition_variable>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace aurea {

class ThumbnailService final : public IMemoryReclaimable {
public:
    struct Image {
        u32 width = 0;
        u32 height = 0;
        std::vector<u8> rgba;   ///< sRGB, alfa 255
    };

    ThumbnailService() = default;
    ~ThumbnailService() override;
    ThumbnailService(const ThumbnailService&) = delete;
    ThumbnailService& operator=(const ThumbnailService&) = delete;

    /// Liga ao orçamento (categoria Thumbnails). Nulo desliga (teto padrão).
    void attach(MemoryManager* memory) noexcept;

    void set_factory(VideoSourceFactory* factory) noexcept { factory_ = factory; }
    void start();
    void stop() noexcept;

    /// Frame de vídeo em cache, ou agenda a decodificação e devolve false.
    /// `assetKey` identifica o asset (AssetId empacotado); `timeUs` é o tempo
    /// DA MÍDIA. Quantizado em 250 ms.
    [[nodiscard]] bool video(u64 assetKey, const Asset& asset, i64 timeUs, u32 height, Image& out);

    /// Miniatura de imagem já decodificada (RGBA8 reto). Síncrono e cacheado.
    [[nodiscard]] bool image(u64 assetKey, const u8* rgba, u32 width, u32 height, u32 thumbHeight, Image& out);

    /// Muda quando uma miniatura nova fica pronta (a UI redesenha).
    [[nodiscard]] u32 generation() const noexcept { return generation_.load(std::memory_order_acquire); }

    /// Projeto fechado: cache e fila zerados, decoders fechados.
    void clear();

    [[nodiscard]] u32 cached() const;
    [[nodiscard]] u64 cached_bytes() const;

    // IMemoryReclaimable
    [[nodiscard]] MemoryClass memory_class() const noexcept override { return MemoryClass::Thumbnails; }
    [[nodiscard]] usize reclaim(usize targetBytes) noexcept override;
    [[nodiscard]] const char* debug_name() const noexcept override { return "miniaturas"; }
    [[nodiscard]] bool accounts_itself() const noexcept override { return true; }
    [[nodiscard]] bool metrics(CacheMetrics& out) const noexcept override;

private:
    struct Key {
        u64 asset = 0;
        i64 bucket = 0;    ///< tempo / 250 ms; -1 para imagem
        u32 height = 0;
        u64 source = 0;    ///< hash do caminho da mídia (relink = chave nova)
        friend bool operator==(const Key&, const Key&) = default;
    };
    struct KeyHash {
        usize operator()(const Key& k) const noexcept {
            return static_cast<usize>(k.asset * 0x9E3779B97F4A7C15ull ^ static_cast<u64>(k.bucket) * 0xC2B2AE3D27D4EB4Full
                                      ^ k.height ^ k.source * 0x165667B19E3779F9ull);
        }
    };
    struct Request {
        Key key;
        Asset asset;     ///< cópia: o modelo pode mudar enquanto decodifica
    };
    struct Decoder {
        u64 asset = 0;
        std::unique_ptr<VideoDecoderBackend> backend;
    };

    void thread_main() noexcept;
    bool decode(const Request& r, Image& out);
    void insert_locked(const Key& k, Image img);
    void pop_lru_locked();
    [[nodiscard]] u64 budget_locked() const noexcept;

    static constexpr i64 kBucketUs = 250'000;
    static constexpr usize kMaxCached = 900;
    /// Teto sem orçamento ligado (testes, motor sem MemoryManager).
    static constexpr u64 kDefaultBudget = 16ull << 20;
    static constexpr usize kMaxQueued = 256;

    VideoSourceFactory* factory_ = nullptr;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Request> queue_;
    std::unordered_map<Key, std::list<std::pair<Key, Image>>::iterator, KeyHash> index_;
    std::list<std::pair<Key, Image>> lru_;     ///< frente = mais recente
    std::unordered_map<Key, bool, KeyHash> pending_;
    std::unordered_map<u64, bool> failedAssets_;   ///< decoder não abriu: não tenta de novo
    std::vector<Decoder> decoders_;            ///< no máximo 2, só a thread mexe
    std::atomic<u32> generation_{0};
    MemoryManager* memory_ = nullptr;
    u64 bytes_ = 0;
    u64 hits_ = 0, misses_ = 0, evictions_ = 0;
    u32 version_ = 0;
    bool running_ = false;
    std::thread thread_;
};

/// Converte um frame decodificado (planos na CPU) em RGBA8 sRGB reduzido para
/// `height` linhas, com a matriz e a faixa do próprio vídeo. Exposto para teste.
[[nodiscard]] bool frame_to_thumbnail(const DecodedFrame& f, u32 height, ThumbnailService::Image& out);

} // namespace aurea

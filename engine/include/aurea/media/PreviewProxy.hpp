#pragma once

#include "aurea/media/VideoSource.hpp"
#include "aurea/export/ExportSink.hpp"
#include "aurea/project/Asset.hpp"
#include <deque>
#include <limits>
#include <unordered_map>

namespace aurea {
class VideoSourceFactory;

struct PreviewProxy {
    std::string path;
    VideoStreamInfo original;
    u32 width = 0, height = 0;
    /// Taxa do arquivo do proxy: a da fonte até 60 (acima disso, dizimada).
    f64 fps = 0.0;
    // Original presentation times; the encoded proxy begins at zero.
    std::vector<i64> times, durations;
};

// -----------------------------------------------------------------------------
// Política do proxy de edição (0.2.5, "Otimizar vídeos pesados").
//
// O custo que trava a edição é o DECODE da fonte, não a resolução do render: a
// escala 1/8 ainda decodifica cada quadro 4K. Por isso a decisão olha o custo
// de decodificar (pixels, fps, codec/bits, bitrate) e vale para QUALQUER classe
// de aparelho — um Snapdragon de topo também engasga com 4K60 HEVC 10 bits.
// A classe só escolhe o tamanho do proxy (720; 540 no LOW).
// -----------------------------------------------------------------------------

/// Ajuste do usuário. Automático: só fonte cara de decodificar. Sempre: também
/// tudo acima de 720p. Desligado: nunca gera nem usa proxy.
enum class ProxyMode : u8 { Automatic = 0, Always = 1, Off = 2 };

/// Estado do proxy de UM asset, como a UI mostra.
enum class ProxyState : u8 { None = 0, Queued = 1, Generating = 2, Ready = 3, Failed = 4 };

struct ProxyStatus {
    ProxyState state = ProxyState::None;
    f32 progress = 0.0f;   ///< 0..1, só em Generating
};

/// Resumo da sessão (assets pedidos desde a abertura do projeto).
struct ProxySummary {
    u32 queued = 0, generating = 0, ready = 0, failed = 0;
    f32 progress = 0.0f;   ///< do proxy em geração (0..1)
    std::string name;      ///< nome do asset em geração
};

/// Limiares da política. Os testes do host baixam os números para exercitar
/// a fila com vídeos sintéticos minúsculos; o app usa os padrões.
struct ProxyThresholds {
    u64 heavyPixels = 1920ull * 1080ull;    ///< acima disto (área) já é pesado
    u64 alwaysPixels = 1280ull * 720ull;    ///< "Sempre" e prévia reduzida: acima disto
    f64 maxFps = 60.0;                      ///< acima disto é pesado (e o proxy é dizimado)
    u64 heavyBitrateBps = 40'000'000ull;    ///< bitrate médio do arquivo
};

inline constexpr u32 kProxyShortSide = 720;      ///< lado menor do proxy
inline constexpr u32 kProxyShortSideLow = 540;   ///< classe LOW
inline constexpr f64 kProxyKeyframeSeconds = 0.5;
/// Falhas seguidas antes de parar de tentar sozinho (só "Gerar de novo" volta).
inline constexpr u32 kProxyMaxFailures = 2;

/// 'hvc1', 'hev1', 'av01', 'vp09': codecs de decode caro por pixel.
[[nodiscard]] bool proxy_complex_codec(u32 codecTag) noexcept;
/// Tag do asset a partir do mime da sondagem ("video/hevc" → 'hvc1'); 0 se desconhecido.
[[nodiscard]] u32 proxy_codec_tag(const char* mime) noexcept;
/// Bitrate médio do arquivo (bps) pelo tamanho e duração; 0 = desconhecido.
[[nodiscard]] u64 proxy_source_bitrate(const Asset& asset) noexcept;
/// Custo de decode acima do 1080p comum: área acima de 1080p, acima de 60 fps,
/// HEVC/AV1/10 bits/HDR a partir de 1080p, ou bitrate muito alto.
[[nodiscard]] bool proxy_source_heavy(const Asset& asset, const ProxyThresholds& t = {}) noexcept;
/// O preview desta fonte usa (e, se preciso, gera) proxy? `reducedPreview`:
/// o usuário escolheu 1/2, 1/4 ou 1/8 — então qualquer fonte acima de 720p.
[[nodiscard]] bool proxy_wanted(const Asset& asset, ProxyMode mode, bool reducedPreview,
                                const ProxyThresholds& t = {}) noexcept;
/// Taxa do proxy: a da fonte, até `maxFps`.
[[nodiscard]] f64 proxy_target_fps(f64 sourceFps, f64 maxFps = 60.0) noexcept;
/// Encoder do proxy: H.264, GOP curto (~0,5 s, seek barato), bitrate moderado.
[[nodiscard]] VideoStreamConfig proxy_stream_config(u32 width, u32 height, f64 fps) noexcept;

/// Dizimação acima de 60 fps: fica o quadro mais próximo de cada passo da grade
/// de saída; os descartados somam na duração do anterior (o tempo não muda).
class ProxyFrameDecimator {
public:
    ProxyFrameDecimator(f64 sourceFps, f64 targetFps) noexcept;
    [[nodiscard]] bool keep(i64 ptsUs) noexcept;
private:
    f64 stepUs_ = 0.0, halfSourceUs_ = 0.0, nextUs_ = 0.0;
    bool decimate_ = false, started_ = false;
};

// CPU area resampling in the source's encoded color space. Crop and rotation
// are baked into the proxy; PQ/HLG stay PQ/HLG (preview quantization to 8 bits).
bool proxy_frame_nv12(const DecodedFrame& frame, u32 width, u32 height,
                      std::vector<u8>& pixels);
std::unique_ptr<VideoDecoderBackend> proxy_decoder(std::unique_ptr<VideoDecoderBackend> decoder,
                                                 std::shared_ptr<const PreviewProxy> proxy);
/// Bugs 0.3.4 ("prévia em 1/4 verde e congelada"): o quadro decodificado do
/// proxy é o mesmo da fonte? (média R'G'B' em 4×4 células da miniatura; o verde
/// de YUV zerado ou um quadro de outro instante não passa). A geração confere o
/// arquivo pronto com esta regra antes de servir (PreviewProxy.cpp).
[[nodiscard]] bool proxy_signature_matches(const DecodedFrame& source, const DecodedFrame& proxy) noexcept;

class PreviewProxyService {
public:
    ~PreviewProxyService();
    void configure(VideoSourceFactory* media, ExportSinkFactory sink, void* context,
                   std::string directory, u64 diskBudget = 2048ull << 20,
                   std::string (*resolve)(const std::string&, void*) = nullptr, void* resolveContext = nullptr);
    /// `shortSide`: lado menor do proxy (0 = nenhum); `pacingMs`: piso da pausa entre quadros.
    void set_policy(u32 shortSide, u32 pacingMs = 0) noexcept;
    void set_thresholds(const ProxyThresholds& thresholds) noexcept;
    /// Trocar o modo tira da fila (e interrompe) o que deixou de ser pedido.
    void set_mode(ProxyMode mode) noexcept;
    [[nodiscard]] ProxyMode mode() const noexcept { return mode_.load(); }
    /// Prévia em 1/2, 1/4 ou 1/8 escolhida pelo usuário (não o AUTO).
    void set_reduced_preview(bool reduced) noexcept;
    /// Chamado (na thread do proxy) quando um proxy fica pronto: o render troca
    /// a fonte no próximo quadro, sem reimportar.
    void set_ready_callback(void (*fn)(void*), void* context) noexcept;
    void set_paused(bool paused) noexcept;
    /// Playback só SEGURA o trabalho (decoder, encoder e progresso ficam);
    /// os outros motivos interrompem o arquivo em curso, que recomeça depois.
    /// A fila sobrevive a qualquer pausa.
    enum PauseReason : u32 { Background = 1, Thermal = 2, Export = 4, Manual = 8, Playback = 16, Capture = 32 };
    void set_pause_reason(PauseReason reason, bool paused) noexcept;
    /// Preview: o proxy pronto, ou nulo (e o asset entra na fila se a política pede).
    std::shared_ptr<const PreviewProxy> request(const Asset& asset);
    [[nodiscard]] ProxyStatus status(const Asset& asset);
    [[nodiscard]] ProxySummary summary();
    /// Tira da fila / interrompe; o asset não volta sozinho até `regenerate`.
    bool cancel(const Asset& asset);
    /// Descarta o arquivo existente (se houver) e gera de novo, à frente da fila.
    bool regenerate(const Asset& asset);
    /// Bytes em disco (proxies, manifestos, parciais).
    [[nodiscard]] u64 cache_bytes() const;
    /// Apaga todos os arquivos; devolve os bytes liberados. A política refaz
    /// o que o preview voltar a pedir.
    u64 clear_cache();
    void clear() noexcept;
    void stop() noexcept;
private:
    struct Request {
        Asset asset; std::string key; u32 target = 0; f64 maxFps = 60.0; u64 generation = 0;
        bool checked = false;   ///< o cache em disco já foi consultado
        bool force = false;     ///< regenerate: ignora o arquivo existente
    };
    struct Entry {
        std::shared_ptr<PreviewProxy> proxy;
        ProxyState state = ProxyState::None;
        u32 failures = 0;
        std::chrono::steady_clock::time_point retryAt{};
        bool dismissed = false;
        std::string name;
    };
    void run();
    std::shared_ptr<PreviewProxy> cached(const Request& request);
    std::shared_ptr<PreviewProxy> generate(const Request& request);
    bool cancelled(u64 generation) const noexcept;
    bool make_room(u64 bytes, const std::string& preserve);
    bool queue_locked(const Asset& asset, const std::string& key, u32 target, bool front, bool force);
    void prune_unwanted_locked(bool interruptCurrent);
    VideoSourceFactory* media_ = nullptr;
    ExportSinkFactory sink_ = nullptr;
    void* context_ = nullptr;
    std::string (*resolve_)(const std::string&, void*) = nullptr;
    void* resolveContext_ = nullptr;
    void (*readyFn_)(void*) = nullptr;
    void* readyCtx_ = nullptr;
    std::string directory_;
    u64 diskBudget_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::deque<Request> queue_;
    std::unordered_map<std::string, Entry> entries_;
    ProxyThresholds thresholds_;          ///< guarded by mutex_
    std::string current_;                 ///< chave em geração; guarded by mutex_
    Asset currentAsset_;                  ///< o asset dela (poda por troca de modo); guarded by mutex_
    std::atomic<f32> progress_{0.0f};
    std::atomic<bool> cancelCurrent_{false};
    std::atomic<u64> generation_{0};
    std::atomic<bool> stopping_{false}, paused_{false}, holding_{false}, reduced_{false};
    std::atomic<ProxyMode> mode_{ProxyMode::Automatic};
    u32 pauseReasons_ = 0;
    std::atomic<u32> shortSide_{0}, pacingMs_{0};
};
} // namespace aurea

// =============================================================================
//  Aurea / media / VideoSource.hpp
//
//  DecodeScheduler de UMA fonte de vídeo: uma thread de decode, um cache, e a
//  política de o que decodificar a seguir.
//
//  A plataforma entrega só o decoder cru (`VideoDecoderBackend`: MediaCodec no
//  Android, VideoToolbox no iOS, sintético nos testes). A INTELIGÊNCIA mora
//  aqui, no núcleo, e é testada no host:
//
//   SEEK COALESCING — o scrub manda 1.0s, 1.1s, 1.2s, 1.3s, 1.4s em
//   sequência rápida. Só o pedido MAIS RECENTE importa: cada novo pedido
//   substitui o anterior (um slot, não uma fila). E se o decoder já está
//   andando para a frente em direção a 1.2s quando chega 1.4s, ele NÃO volta ao
//   keyframe: continua andando e entrega 1.4s. Só um pedido para trás, ou longe
//   demais à frente, custa um seek.
//
//   FRAMES INTERMEDIÁRIOS NÃO SÃO ENTREGUES — no scrub, os frames entre o
//   keyframe e o alvo são decodificados (é inevitável em H.264) mas liberados
//   sem render: não ocupam buffer do ImageReader, não vão para a GPU.
//
//   PREFETCH POR MODO (§16) — tocando para a frente, a fonte mantém o atual,
//   o próximo e o seguinte prontos. Arrastando para a frente, aproveita o
//   embalo do decoder e deixa alguns à frente. PARA TRÁS (reverso, scrub
//   descendo) não há embalo, mas o seek até o alvo decodifica os frames
//   anteriores de qualquer jeito: em vez de jogá-los fora, os últimos
//   `backFrames_` antes do alvo vão para o cache — os próximos passos para
//   trás caem nele sem seek. No reverso tocando, a janela é reabastecida
//   quando sobra menos de um frame atrás do playhead. Parado ou congelado
//   (time remap sem movimento, direção 0): só o alvo.
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/media/DecodedFrameCache.hpp"
#include "aurea/media/VideoTypes.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace aurea {

/// O decoder cru da plataforma. Chamado SÓ pela thread de decode da fonte.
class VideoDecoderBackend {
public:
    virtual ~VideoDecoderBackend() = default;

    [[nodiscard]] virtual const VideoStreamInfo& info() const noexcept = 0;

    /// Posiciona no keyframe mais próximo ANTES de `targetUs` e descarta o que
    /// estava em voo no decoder.
    [[nodiscard]] virtual Status seek_to_keyframe(i64 targetUs) noexcept = 0;

    /// Decodifica o próximo frame em ordem de APRESENTAÇÃO. Frames com
    /// pts < `deliverFromUs` são decodificados e descartados sem produzir
    /// imagem (render = false no MediaCodec). Devolve o pts em `outPtsUs` nos
    /// dois casos; `out` só é preenchido quando entregue.
    [[nodiscard]] virtual Status next_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs,
                                            bool& endOfStream) noexcept = 0;

    /// Quantos frames entregues podem estar vivos ao mesmo tempo (buffers do
    /// ImageReader). O cache é dimensionado abaixo disso.
    [[nodiscard]] virtual u32 max_live_frames() const noexcept { return 8; }

    /// Distância típica entre keyframes. Abaixo dela, andar para a frente é
    /// mais barato que fazer seek.
    [[nodiscard]] virtual i64 keyframe_interval_us() const noexcept { return 2'000'000; }

    /// App em segundo plano: libera o codec de hardware (recurso escasso do
    /// sistema). `resume` recria e volta à posição.
    virtual void suspend() noexcept {}
    [[nodiscard]] virtual Status resume() noexcept { return OkStatus; }
};

enum class DecodeMode : u8 {
    Idle = 0,
    Still,      ///< parado: um frame, exato
    Scrub,      ///< arrastando: o mais recente, rápido; intermediários descartados
    Playback,   ///< tocando: sequencial, com antecedência
};

/// Prioridade de uso. Preview disputa com nada; miniatura cede para tudo;
/// export tem decoder próprio e não compartilha com o preview.
enum class MediaPriority : u8 { Preview = 0, Export, Thumbnail };

struct DecodeRequest {
    i64 targetUs = 0;
    DecodeMode mode = DecodeMode::Still;
    i32 direction = 0;     ///< +1 para a frente, -1 para trás, 0 parado
    f32 speed = 1.0f;
    bool retainPreroll = false; ///< Temporal effects reuse bounded nearby frames decoded during a seek.
};

class VideoSource {
public:
    struct Stats {
        u64 framesDelivered = 0;
        u64 framesDiscarded = 0;    ///< decodificados e liberados sem render
        u64 seeks = 0;
        u64 requests = 0;
        u64 coalesced = 0;          ///< pedidos que substituíram outro ainda não atendido
        u64 forwardRetargets = 0;   ///< novo alvo alcançado andando, sem seek
        f32 decodeMsAvg = 0.0f;     ///< por frame, média móvel
        f32 lastSeekMs = 0.0f;      ///< do pedido até o frame pronto
        DecodedFrameCache::Stats cache{};
        bool endOfStream = false;
    };

    VideoSource(std::unique_ptr<VideoDecoderBackend> backend, MediaPriority priority);
    ~VideoSource();

    VideoSource(const VideoSource&) = delete;
    VideoSource& operator=(const VideoSource&) = delete;

    void start();
    void stop() noexcept;

    [[nodiscard]] VideoStreamInfo info() const noexcept {
        std::lock_guard<std::mutex> lock(infoMutex_);
        return publishedInfo_;
    }
    [[nodiscard]] i64 frame_duration_us() const noexcept { return frameUs_; }

    /// Pedido mais recente. Substitui o anterior (coalescência).
    void request(const DecodeRequest& r) noexcept;

    /// Melhor frame disponível agora para `targetUs`. Nunca bloqueia.
    [[nodiscard]] FrameRef frame_for(i64 targetUs, bool* exact) noexcept;

    /// Chamado (na thread de decode) quando chega um frame novo. O motor usa
    /// para acordar o render quando estava esperando por ele.
    void set_ready_callback(void (*fn)(void*), void* ctx) noexcept;

    /// Espera (teste/export) até o frame exato estar no cache ou o tempo
    /// acabar. Nunca chamado no preview.
    [[nodiscard]] bool wait_for(i64 targetUs, u32 timeoutMs) noexcept;

    void suspend() noexcept;
    void resume() noexcept;

    [[nodiscard]] Stats stats() const noexcept;
    [[nodiscard]] DecodedFrameCache& cache() noexcept { return cache_; }

private:
    void thread_main() noexcept;
    [[nodiscard]] bool reachable_forward(i64 needUs) const noexcept;
    void deliver(FrameRef frame) noexcept;
    void schedule_retry(u64 generation) noexcept;
    void publish_info() noexcept;

    std::unique_ptr<VideoDecoderBackend> backend_;
    MediaPriority priority_;
    DecodedFrameCache cache_;
    mutable std::mutex infoMutex_;
    VideoStreamInfo publishedInfo_{};
    i64 frameUs_ = 33'333;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable delivered_;
    DecodeRequest request_{};
    u64 requestGen_ = 0;
    u64 handledGen_ = 0;
    u64 requestTimeNs_ = 0;
    u32 requestCacheVersion_ = 0;
    u64 retryAfterNs_ = 0;
    u32 retryAttempts_ = 0;
    bool running_ = false;
    bool suspended_ = false;
    bool suspendApplied_ = false;

    // Estado do decoder — só a thread de decode toca.
    i64 decoderPosUs_ = -1;        ///< pts do último frame que saiu do decoder
    bool decoderValid_ = false;
    bool eos_ = false;
    /// Para trás (reverso, scrub descendo): quantos frames ANTES do alvo são
    /// entregues ao cache quando o seek já vai decodificá-los de qualquer
    /// jeito (§16). Cabe no cache junto com o atual.
    i64 backFrames_ = 0;
    /// Alvo do último preenchimento para trás tentado: um por alvo, senão um
    /// começo de mídia sem frame em 0 viraria laço de seek.
    i64 backfillAttemptUs_ = -1;

    void (*readyFn_)(void*) = nullptr;
    void* readyCtx_ = nullptr;

    mutable std::mutex statsMutex_;
    Stats stats_{};

    std::thread thread_;
};

} // namespace aurea

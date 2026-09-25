// =============================================================================
//  Aurea / media / MediaManager.hpp
//
//  As fontes de mídia abertas do projeto.
//
//  Uma fonte de vídeo POR LAYER (não por asset): duas layers do mesmo arquivo
//  em instantes diferentes com um decoder só fariam o decoder saltar de um
//  instante para o outro a cada frame — um seek por frame. Decoders de hardware
//  são escassos (o sistema dá de 4 a 16), então fontes paradas são fechadas.
//
//  A plataforma entra pela fábrica: MediaCodec no Android, VideoToolbox no iOS,
//  sintética nos testes. O núcleo não sabe qual.
// =============================================================================
#pragma once

#include "aurea/audio/Audio.hpp"
#include "aurea/core/Handle.hpp"
#include "aurea/media/VideoSource.hpp"
#include "aurea/project/Asset.hpp"

#include <memory>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace aurea {

/// Informação obtida ao importar (sondagem do arquivo).
struct MediaProbe {
    VideoStreamInfo video{};
    bool hasVideo = false;
    bool hasAudio = false;
    u32 audioSampleRate = 0;
    u32 audioChannels = 0;
    i64 audioDurationUs = 0;
};

class VideoSourceFactory {
public:
    virtual ~VideoSourceFactory() = default;
    /// Lê os metadados do arquivo (sem decodificar). Chamado na importação,
    /// fora do lock do modelo — pode levar alguns ms.
    [[nodiscard]] virtual bool probe(const char* sourcePath, MediaProbe& out) = 0;
    /// Abre um decoder para o asset. `nullptr` quando o arquivo não abre.
    [[nodiscard]] virtual std::unique_ptr<VideoDecoderBackend> open_video(const Asset& asset,
                                                                          MediaPriority priority) = 0;
    /// Abre a trilha de áudio de um arquivo (caminho já resolvido). `nullptr`
    /// = sem áudio ou plataforma sem decoder de áudio.
    [[nodiscard]] virtual std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char* sourcePath) {
        (void)sourcePath;
        return nullptr;
    }
};

class MediaManager {
public:
    MediaManager() = default;
    ~MediaManager();

    void set_factory(VideoSourceFactory* factory) noexcept;
    /// Orçamento de quadros decodificados (Fase 8 §12): cada fonte nova liga o
    /// cache dela à categoria DecodedFrames, compartilhada entre as fontes.
    void set_memory(MemoryManager* memory) noexcept;
    [[nodiscard]] VideoSourceFactory* factory() const noexcept { return factory_; }

    /// Fonte da layer. Abre fora da thread de render; nula enquanto abre.
    [[nodiscard]] VideoSource* source_for(LayerId layer, AssetId assetId, const Asset& asset,
                                          u64 frameNumber);

    /// Retira fontes ociosas; a fila de encerramento fecha codecs fora do render.
    void collect(u64 frameNumber, u32 idleFrames = 180);

    /// Retira a fonte de uma layer sem esperar pelo decoder na thread da UI.
    void close_layer(LayerId layer);
    /// Fecha e espera todas as fontes, inclusive as já retiradas, antes de
    /// substituir/destruir o projeto, a fábrica ou o contexto dos callbacks.
    void close_all();

    void suspend_all();
    void resume_all();

    /// Callback de frame pronto, repassado a toda fonte.
    void set_ready_callback(void (*fn)(void*), void* ctx) noexcept;

    struct Stats {
        u32 sources = 0;
        u32 cachedFrames = 0;
        u64 cachedBytes = 0;
        f32 decodeMsAvg = 0.0f;
        f32 lastSeekMs = 0.0f;
        u64 seeks = 0;
        u64 coalesced = 0;
        u64 discarded = 0;
        bool hardwareDecoder = false;
        char decoderName[64] = {};
    };
    [[nodiscard]] Stats stats() const;

private:
    struct Opening {
        std::unique_ptr<VideoDecoderBackend> decoder;
        std::atomic<bool> ready{false};
        std::thread worker;
        ~Opening() { if (worker.joinable()) worker.join(); }
    };
    struct Entry {
        LayerId layer{};
        AssetId asset{};
        std::unique_ptr<VideoSource> source;
        std::unique_ptr<Opening> opening;
        u64 lastUsedFrame = 0;
        bool failed = false;
    };

    VideoSourceFactory* factory_ = nullptr;
    MemoryManager* memory_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    void (*readyFn_)(void*) = nullptr;
    void* readyCtx_ = nullptr;
    bool suspended_ = false;

    // One retirement worker: a slow platform codec shutdown must not join on
    // the render/UI thread. close_all drains it before factories/callbacks die.
    void retire_locked(Entry&& entry);
    void retire_main();
    void drain_retired();
    std::mutex retireMutex_;
    std::condition_variable retireWake_;
    std::deque<Entry> retired_;
    std::thread retireThread_;
    bool retiring_ = false;
    bool retireNotifying_ = false;
    bool retireStop_ = false;
};

} // namespace aurea

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

#include "aurea/core/Handle.hpp"
#include "aurea/media/VideoSource.hpp"
#include "aurea/project/Asset.hpp"

#include <memory>
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
};

class MediaManager {
public:
    MediaManager() = default;
    ~MediaManager();

    void set_factory(VideoSourceFactory* factory) noexcept;
    [[nodiscard]] VideoSourceFactory* factory() const noexcept { return factory_; }

    /// Fonte da layer. Abre na primeira vez (a abertura é síncrona e custa
    /// alguns ms: acontece no primeiro frame em que a layer aparece).
    [[nodiscard]] VideoSource* source_for(LayerId layer, AssetId assetId, const Asset& asset,
                                          u64 frameNumber);

    /// Fecha fontes que ninguém pediu nos últimos `idleFrames`.
    void collect(u64 frameNumber, u32 idleFrames = 180);

    /// Fecha a fonte de uma layer (layer apagada, projeto fechado).
    void close_layer(LayerId layer);
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
    struct Entry {
        LayerId layer{};
        AssetId asset{};
        std::unique_ptr<VideoSource> source;
        u64 lastUsedFrame = 0;
        bool failed = false;
    };

    VideoSourceFactory* factory_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    void (*readyFn_)(void*) = nullptr;
    void* readyCtx_ = nullptr;
    bool suspended_ = false;
};

} // namespace aurea

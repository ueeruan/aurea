// =============================================================================
//  Aurea / project / Asset.hpp
//
//  Metadados de um pedaço de mídia importado.
//
//  O que fica aqui: o que o motor precisa saber para planejar decode, cache e
//  proxy SEM abrir o arquivo de novo. Abrir o container de um 4K HEVC custa
//  dezenas de ms; fazer isso ao arrastar uma layer na timeline travaria o
//  arrasto.
//
//  Portanto os metadados são lidos UMA vez, na importação, e guardados no
//  .aurea. O `sourcePath` é a referência; `proxyPath` é gerado sob demanda.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Handle.hpp"

#include <string>
#include <vector>

namespace aurea {

enum class AssetKind : u8 {
    Unknown = 0,
    Video,
    Audio,
    Image,
    Font,
    Model3D,
    Environment,   ///< HDRI / IBL
    Lut,
    TemplateProject,
    Shape,         ///< SVG / vetor importado
};

/// Um parâmetro de codec que o aparelho pode ou não aceitar. Guardado por
/// asset porque o mesmo projeto pode misturar H.264, HEVC 10-bit e AV1 — e o
/// pipeline se adapta por layer, não por projeto.
struct MediaProfile {
    u32 codecTag      = 0;       ///< 'avc1', 'hvc1', 'av01', ...
    u32 profile       = 0;
    u32 level         = 0;
    u8  bitDepth      = 8;
    u8  chromaSubsampling = 0;   ///< 0 = 4:2:0, 1 = 4:2:2, 2 = 4:4:4
    bool hdr          = false;
    ColorSpace transfer = ColorSpace::Unknown;   ///< sRGB, HLG, PQ
    ColorSpace primaries = ColorSpace::Unknown;  ///< Rec709, Rec2020, P3
};

struct VideoTrackInfo {
    u32 index        = 0;
    u32 width        = 0;
    u32 height       = 0;
    f64 fps          = 30.0;
    FrameIndex frameCount{0};
    bool variableFrameRate = false;
    /// Lista de tamanhos de amostra, quando o container é de taxa variável.
    /// Sem isto, buscar um frame no meio de um VFR é chute.
    std::vector<u32> sampleSizes;
    std::vector<i64> sampleTimestamps;   ///< em unidades do timescale
    u32 timescale = 1000;
};

struct AudioTrackInfo {
    u32 index      = 0;
    u32 sampleRate = 48000;
    u32 channels   = 2;
    FrameIndex sampleCount{0};
};

struct Asset {
    AssetKind kind = AssetKind::Unknown;
    std::string name;

    /// Caminho na sandbox do app. Sempre relativo à raiz do projeto quando
    /// dentro dela, absoluto quando é mídia da galeria/arquivos.
    std::string sourcePath;

    /// Proxy gerado automaticamente para preview. Vazio se não há.
    std::string proxyPath;
    u32  proxyWidth  = 0;
    u32  proxyHeight = 0;

    /// Miniatura (cartão da timeline + biblioteca).
    std::string thumbnailPath;

    /// Waveform pré-computada (áudio). Um arquivo plano de picos por bucket —
    /// desenhar a waveform lendo o áudio inteiro a cada repaint seria absurdo.
    std::string waveformPath;
    u32  waveformBuckets = 0;

    // --- Metadados de mídia ---------------------------------------------------
    MediaProfile   profile{};
    VideoTrackInfo video{};
    AudioTrackInfo audio{};

    /// Duração total. Para imagem e fonte, zero.
    FrameIndex duration{0};
    f64        timebaseFps = 30.0;

    u64 fileSizeBytes = 0;
    u64 contentHash   = 0;   ///< usado como chave de cache e para detectar troca

    /// Nome do arquivo original, para o usuário reconhecer o que importou
    /// mesmo depois de o app renomear tudo na sandbox.
    std::string originalFilename;

    // --- 3D -------------------------------------------------------------------
    struct ModelInfo {
        u32 meshCount     = 0;
        u32 materialCount = 0;
        u32 animationCount = 0;
        u32 triangleCount  = 0;
        u32 lodCount       = 0;
        bool hasSkeleton   = false;
        bool hasMorphTargets = false;
        std::vector<std::string> animationNames;
        std::vector<u32> lodTriangleCounts;
    } model;

    [[nodiscard]] bool is_visual() const noexcept {
        return kind == AssetKind::Video || kind == AssetKind::Image
            || kind == AssetKind::Model3D || kind == AssetKind::Shape;
    }
    [[nodiscard]] bool has_video() const noexcept { return video.width > 0 && video.height > 0; }
    [[nodiscard]] bool has_audio() const noexcept { return audio.channels > 0; }
    [[nodiscard]] bool proxy_ready() const noexcept { return !proxyPath.empty(); }
};

} // namespace aurea

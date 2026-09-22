// =============================================================================
//  Aurea / core / Types.hpp
//
//  Tipos básicos do motor. Nada aqui conhece GPU, plataforma ou UI.
//
//  Identificadores são handles de geração (ver Handle.hpp) e não ponteiros:
//  a UI guarda o handle, o motor resolve o ponteiro. Assim uma layer apagada
//  na timeline nunca vira um ponteiro pendurado na UI, e o mesmo id viaja
//  limpo pela bridge JNI / ObjC++.
// =============================================================================
#pragma once

#include "aurea/core/Version.hpp"

#include <cstdint>
#include <cstddef>
#include <compare>
#include <limits>

namespace aurea {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;

// -----------------------------------------------------------------------------
// Tempo
//
//  O motor inteiro trabalha em dois relógios distintos e nunca os mistura:
//
//    FrameIndex  — posição discreta em uma grade de frames. É o que a timeline
//                  guarda, o que o cache usa como chave e o que o export
//                  percorre. Inteiro, exato, sem drift.
//
//    TickNs      — nanossegundos. É o que o master clock de áudio produz e o
//                  que o renderer consulta. Só converte para FrameIndex na
//                  borda, com a taxa da composição em mãos.
//
//  Misturar os dois é a origem clássica de drift A/V. Aqui o compilador ajuda:
//  são tipos distintos, não dois `int64`.
// -----------------------------------------------------------------------------
struct FrameIndex {
    i64 value = 0;
    constexpr FrameIndex() = default;
    constexpr explicit FrameIndex(i64 v) noexcept : value(v) {}
    friend constexpr bool operator==(FrameIndex, FrameIndex) noexcept = default;
    friend constexpr auto operator<=>(FrameIndex, FrameIndex) noexcept = default;
};

struct TickNs {
    i64 value = 0;
    constexpr TickNs() = default;
    constexpr explicit TickNs(i64 v) noexcept : value(v) {}
    friend constexpr bool operator==(TickNs, TickNs) noexcept = default;
    friend constexpr auto operator<=>(TickNs, TickNs) noexcept = default;
};

/// Converte um instante em nanossegundos para o índice de frame da grade.
/// Sempre arredonda para baixo: o frame N cobre [N, N+1).
[[nodiscard]] constexpr FrameIndex frame_at(TickNs t, f64 fps) noexcept {
    if (fps <= 0.0) return FrameIndex{0};
    const f64 seconds = static_cast<f64>(t.value) * 1e-9;
    const f64 frames  = seconds * fps;
    // piso exato sem depender de std::floor em constexpr lento
    const i64 i = static_cast<i64>(frames);
    return FrameIndex{(frames < 0.0 && frames != static_cast<f64>(i)) ? i - 1 : i};
}

/// Instante de início do frame `f`.
///
/// A implementação parece redundante — calcula, converte e depois CONFERE — e é
/// de propósito. A identidade que precisa valer é
///
///     frame_at(tick_at(f, fps), fps) == f
///
/// e ela não sai de graça em ponto flutuante. `f * 1e9 / fps` não é exato: a
/// 29,97 fps o valor calculado pode cair um nanossegundo ANTES do início real
/// do frame, e então `frame_at` devolve `f - 1`. O erro é de um frame, aparece
/// só em algumas taxas, e o sintoma é um frame repetido no meio de um vídeo
/// longo — caro de encontrar e barato de prevenir.
///
/// O laço de correção roda zero ou uma vez na prática; o teto existe para
/// garantir que ele termina mesmo com um `fps` absurdo.
[[nodiscard]] constexpr TickNs tick_at(FrameIndex f, f64 fps) noexcept {
    if (fps <= 0.0) return TickNs{0};
    const f64 seconds = static_cast<f64>(f.value) / fps;
    i64 t = static_cast<i64>(seconds * 1e9 + 0.5);

    for (int guard = 0; guard < 4; ++guard) {
        const FrameIndex back = frame_at(TickNs{t}, fps);
        if (back.value == f.value) break;
        // `t` caiu fora do frame dele: anda um nanossegundo na direção certa.
        t += (back.value < f.value) ? 1 : -1;
    }
    return TickNs{t};
}

/// Duração de um frame em nanossegundos, arredondada.
[[nodiscard]] constexpr TickNs frame_duration(f64 fps) noexcept {
    if (fps <= 0.0) return TickNs{0};
    return TickNs{static_cast<i64>(1e9 / fps + 0.5)};
}

// -----------------------------------------------------------------------------
// Limites
// -----------------------------------------------------------------------------
inline constexpr u32 kInvalidIndex    = std::numeric_limits<u32>::max();
inline constexpr u32 kMaxLayerCount   = 4096;   // por composição
inline constexpr u32 kMaxTrackCount   = 512;    // por layer
inline constexpr u32 kMaxKeyframes    = 1u << 20;
inline constexpr u32 kMaxEffectCount  = 64;     // por layer
inline constexpr u32 kMaxMaskCount    = 32;     // por layer
inline constexpr u32 kMaxNestingDepth = 8;      // composições aninhadas

// -----------------------------------------------------------------------------
// Enums do modelo — declarados aqui porque a serialização, a UI e o renderer
// precisam concordar. Valor numérico é contrato de arquivo .aurea: nunca
// reordene, só acrescente no fim.
// -----------------------------------------------------------------------------
enum class LayerKind : u16 {
    Unknown = 0,
    Video   = 1,
    Image   = 2,
    Audio   = 3,
    Text    = 4,
    Shape   = 5,
    Null    = 6,
    Adjustment = 7,
    Camera  = 8,
    Light   = 9,
    Model3D = 10,
    ParticleSystem = 11,
    Composition = 12,   // composição aninhada (pre-comp)
};

enum class BlendMode : u16 {
    Normal = 0,
    Add, Subtract, Multiply, Screen, Overlay, Darken, Lighten,
    ColorDodge, ColorBurn, HardLight, SoftLight, Difference, Exclusion,
    Hue, Saturation, Color, Luminosity,
};

enum class Interpolation : u8 {
    Hold = 0,
    Linear,
    Bezier,
    EaseIn,
    EaseOut,
    EaseInOut,
    CustomCurve,
};

enum class MaskOperation : u8 {
    Add = 0, Subtract, Intersect, Difference,
};

enum class TrackProperty : u16 {
    // Transform
    PositionX = 0, PositionY, PositionZ,
    ScaleX, ScaleY, ScaleZ,
    RotationX, RotationY, RotationZ,
    AnchorX, AnchorY, AnchorZ,
    Opacity,
    SkewX, SkewY,
    // Câmera
    Fov, FocalLength, FocusDistance, Aperture, NearPlane, FarPlane,
    // Luz
    LightIntensity, LightColorR, LightColorG, LightColorB,
    LightConeAngle, LightPenumbra,
    // Material / 3D
    MaterialMetallic, MaterialRoughness,
    // Texto
    TextTracking,
    // Time remap
    TimeRemap,
    // Efeito (o índice do parâmetro vem junto no TrackRef)
    EffectParam,
    // Áudio (no fim: os números anteriores estão gravados em projetos)
    AudioVolume,       ///< volume linear (1 = 100%), animável
    TextAnimParam,     ///< parâmetro de animador de texto (effectIndex = animador, paramIndex = TextAnimParam)
    _Count,
};

enum class ExportCodec : u16 {
    H264 = 0, HEVC, AV1, ProRes,
};

enum class AudioCodec : u16 {
    AAC = 0, PCM,
};

enum class ColorSpace : u16 {
    Unknown = 0,
    SRGB, Rec709, DisplayP3, Rec2020, HDR10, HLG,
};

/// Layout de memória de um frame de vídeo decodificado. Os valores novos
/// entram SEMPRE no fim: o número pode ter sido gravado em cache de proxy.
enum class PixelFormat : u16 {
    Unknown = 0,
    RGBA8, RGBA16F,
    NV12,     ///< Y + CbCr intercalado, 8 bits (o caso comum de H.264)
    P010,     ///< Y + CbCr intercalado, 10 bits em 16 (HEVC Main10, HDR)
    NV21,     ///< Y + CrCb intercalado (câmeras antigas)
    YUV420P,  ///< três planos separados, 8 bits (I420)
    /// Formato opaco do fabricante: só a GPU sabe ler, via conversão YCbCr.
    /// É o que o MediaCodec entrega no caminho zero-copy.
    Opaque,
};

/// Formato de textura da GPU. Os valores novos entram no fim, pelo mesmo
/// motivo do PixelFormat.
enum class SurfaceFormat : u16 {
    R8 = 0, RG8, RGBA8, R16F, RGBA16F, R32F, Depth24, Depth32F,
    R16,      ///< unorm 16 — plano Y de P010 enviado pela CPU
    RG16,     ///< unorm 16 — plano CbCr de P010
    BGRA8,
    RG16F,
    RGBA32F,
    // Texturas de material (3D). A GPU lineariza o sRGB ao amostrar — o filtro
    // bilinear e os mipmaps ficam corretos (filtrar em sRGB escurece bordas).
    RGBA8_sRGB,
    // Comprimidos por bloco 4×4 (KTX2/Basis transcodificado). 1 byte/texel.
    BC7, BC7_sRGB,
    ETC2_RGBA8, ETC2_RGBA8_sRGB,
    ASTC4x4, ASTC4x4_sRGB,
};

enum class ScalingMode : u16 {
    Fit = 0, Fill, Stretch,
};

/// Escala de renderização do preview. Mora aqui, e não em DeviceCapabilities,
/// porque é uma preferência que o PROJETO guarda (o usuário reabre e encontra a
/// tela como deixou) — e o arquivo .aurea não deve carregar o cabeçalho de
/// plataforma só para gravar um byte.
enum class PreviewScale : u8 {
    Auto = 0,   ///< o controlador adaptativo decide
    Full,
    Half,
    Quarter,
    Eighth,
};

} // namespace aurea

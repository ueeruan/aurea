// =============================================================================
//  Aurea / media / VideoTypes.hpp
//
//  O vocabulário de vídeo do motor: metadados de cor, informação do stream e o
//  frame decodificado. Nada aqui conhece MediaCodec, VideoToolbox ou Vulkan.
//
//  METADADOS DE COR SÃO OBRIGATÓRIOS NA CONVERSÃO. Um vídeo de celular pode ser
//  BT.709 limitado (o comum), BT.601 (câmera antiga, vídeo de mensageiro),
//  faixa completa (captura de tela), BT.2020 PQ/HLG (HDR do próprio aparelho).
//  Assumir "tudo é BT.709 limitado" desloca a cor de metade dos vídeos — o
//  vermelho puxa para o laranja, o preto vira cinza — e ninguém sabe dizer por
//  quê. A conversão lê estes campos, sempre.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <atomic>

namespace aurea {

/// Curva de transferência (valores batem com AUREA_TF_* em color.glsl).
enum class TransferFunction : u8 { SRGB = 0, Linear = 1, PQ = 2, HLG = 3 };

/// Primárias (valores batem com AUREA_PRIM_* em color.glsl).
enum class ColorPrimaries : u8 { BT709 = 0, BT2020 = 1, P3 = 2, BT601 = 3 };

/// Matriz Y'CbCr → R'G'B'.
enum class YCbCrMatrix : u8 { BT601 = 0, BT709, BT2020 };

struct VideoColorInfo {
    YCbCrMatrix      matrix = YCbCrMatrix::BT709;
    bool             fullRange = false;
    ColorPrimaries   primaries = ColorPrimaries::BT709;
    TransferFunction transfer = TransferFunction::SRGB;
    u8               bitDepth = 8;
    /// Quais campos vieram do arquivo (o resto foi deduzido). Vai para o painel
    /// DEV: "cor deduzida" explica um vídeo que o usuário acha estranho.
    bool             fromStream = false;

    [[nodiscard]] bool hdr() const noexcept {
        return transfer == TransferFunction::PQ || transfer == TransferFunction::HLG;
    }

    /// Coeficientes Kr, Kb da matriz.
    void coefficients(f32& kr, f32& kb) const noexcept {
        switch (matrix) {
            case YCbCrMatrix::BT601:  kr = 0.299f;  kb = 0.114f;  return;
            case YCbCrMatrix::BT709:  kr = 0.2126f; kb = 0.0722f; return;
            case YCbCrMatrix::BT2020: kr = 0.2627f; kb = 0.0593f; return;
        }
        kr = 0.2126f; kb = 0.0722f;
    }

    /// Dedução quando o arquivo não diz: o que players e o próprio Android
    /// fazem. Abaixo de 720 linhas é conteúdo SD (BT.601); acima, HD (BT.709).
    [[nodiscard]] static VideoColorInfo guess(u32 width, u32 height, u8 bitDepth) noexcept {
        VideoColorInfo c;
        const u32 lines = width < height ? width : height;
        if (lines < 720) {
            c.matrix = YCbCrMatrix::BT601;
            c.primaries = ColorPrimaries::BT601;
        }
        c.bitDepth = bitDepth;
        return c;
    }

    friend bool operator==(const VideoColorInfo&, const VideoColorInfo&) noexcept = default;
};

/// O que se sabe de um stream de vídeo ao abrir o arquivo.
struct VideoStreamInfo {
    u32  codedWidth = 0;      ///< tamanho do buffer do decoder (alinhado)
    u32  codedHeight = 0;
    u32  cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0;   ///< inclusivo, em px codificados
    u32  rotation = 0;        ///< 0, 90, 180, 270 — metadado do container
    f64  fps = 30.0;
    i64  durationUs = 0;
    VideoColorInfo color{};
    char codec[32] = {};      ///< "video/avc", "video/hevc"
    char decoderName[64] = {};
    bool hardwareDecoder = false;

    /// Tamanho visível (sem a sobra de alinhamento), antes da rotação.
    [[nodiscard]] u32 visible_width() const noexcept {
        return cropRight >= cropLeft && cropRight ? cropRight - cropLeft + 1 : codedWidth;
    }
    [[nodiscard]] u32 visible_height() const noexcept {
        return cropBottom >= cropTop && cropBottom ? cropBottom - cropTop + 1 : codedHeight;
    }
    /// Tamanho de exibição, já girado — é o tamanho da layer.
    [[nodiscard]] u32 display_width() const noexcept {
        return (rotation == 90 || rotation == 270) ? visible_height() : visible_width();
    }
    [[nodiscard]] u32 display_height() const noexcept {
        return (rotation == 90 || rotation == 270) ? visible_width() : visible_height();
    }
};

// -----------------------------------------------------------------------------
// Frame decodificado, com contagem de referências.
//
// Quem segura um frame: o cache (enquanto ele pode ser útil), o renderer
// (durante o frame que o usa) e o backend (até a GPU terminar de lê-lo). O
// buffer só volta ao decoder quando o último solta — e esse "último" é quase
// sempre o `defer_until_gpu_done` do backend, que é exatamente o momento certo.
// -----------------------------------------------------------------------------
class DecodedFrame {
public:
    virtual ~DecodedFrame() = default;

    i64 ptsUs = 0;
    u32 width = 0;          ///< codificado
    u32 height = 0;
    u32 cropLeft = 0, cropTop = 0, visibleWidth = 0, visibleHeight = 0;
    u32 rotation = 0;
    PixelFormat format = PixelFormat::Opaque;
    VideoColorInfo color{};

    /// Caminho zero-copy: AHardwareBuffer* (Android) / CVPixelBufferRef (iOS).
    void* hardwareBuffer = nullptr;

    /// Caminho de fallback: planos na memória (Y, CbCr ou Cb, Cr).
    const u8* planes[3] = {nullptr, nullptr, nullptr};
    u32 strides[3] = {0, 0, 0};
    u32 planeCount = 0;

    /// Identidade estável do buffer do decoder (para o backend cachear a
    /// importação: o mesmo AHardwareBuffer volta a cada N frames).
    u64 bufferId = 0;

    [[nodiscard]] u64 approx_bytes() const noexcept {
        const u64 bpp = format == PixelFormat::P010 ? 2 : 1;
        return static_cast<u64>(width) * height * bpp * 3 / 2;
    }

    void add_ref() noexcept { refs_.fetch_add(1, std::memory_order_relaxed); }
    void release() noexcept {
        if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
    }

private:
    std::atomic<u32> refs_{1};
};

/// Referência a um frame (intrusiva, sem alocação extra).
class FrameRef {
public:
    FrameRef() noexcept = default;
    /// Adota a referência inicial de um frame recém-criado.
    static FrameRef adopt(DecodedFrame* f) noexcept { FrameRef r; r.f_ = f; return r; }

    FrameRef(const FrameRef& o) noexcept : f_(o.f_) { if (f_) f_->add_ref(); }
    FrameRef(FrameRef&& o) noexcept : f_(o.f_) { o.f_ = nullptr; }
    FrameRef& operator=(const FrameRef& o) noexcept {
        if (this != &o) { reset(); f_ = o.f_; if (f_) f_->add_ref(); }
        return *this;
    }
    FrameRef& operator=(FrameRef&& o) noexcept {
        if (this != &o) { reset(); f_ = o.f_; o.f_ = nullptr; }
        return *this;
    }
    ~FrameRef() { reset(); }

    void reset() noexcept { if (f_) { f_->release(); f_ = nullptr; } }
    /// Solta a posse sem decrementar (quem recebe fica responsável).
    [[nodiscard]] DecodedFrame* detach() noexcept { DecodedFrame* f = f_; f_ = nullptr; return f; }

    [[nodiscard]] DecodedFrame* get() const noexcept { return f_; }
    [[nodiscard]] DecodedFrame* operator->() const noexcept { return f_; }
    [[nodiscard]] explicit operator bool() const noexcept { return f_ != nullptr; }

private:
    DecodedFrame* f_ = nullptr;
};

} // namespace aurea

// =============================================================================
//  Aurea / platform / AndroidVideoPath.hpp
//
//  Por onde o quadro do MediaCodec chega ao renderer no Android — a parte PURA
//  (sem NDK), testada no host.
//
//  Os bugs de vídeo por GPU (Samsung fechando, listras no PowerVR, preview
//  piscando no Immortalis) tinham a mesma raiz: o motor interpretava sozinho o
//  buffer do decoder — conversão YCbCr externa no Vulkan, ou planos YUV lidos
//  pela CPU com passo/fatia/crop de cada fabricante. O app antigo não tinha
//  nenhum desses bugs porque deixava o DRIVER fazer isso: SurfaceTexture +
//  samplerExternalOES. O caminho padrão agora é o mesmo, sem Java:
//
//   DriverGl        MediaCodec → AImageReader PRIVATE → AHardwareBuffer →
//                   EGLImage → GL_TEXTURE_EXTERNAL_OES (o driver converte YUV,
//                   passo, fatia, AFBC) → um quadrilátero com o crop nas
//                   coordenadas → AHardwareBuffer RGBA8 → Vulkan como imagem
//                   R8G8B8A8 comum (sem YCbCr: importação que todo driver faz).
//   CpuPlanes       decoder de hardware → AImageReader YUV_420_888 → planos
//                   pela CPU (media::copy_decoded_yuv420) → upload.
//   SoftwarePlanes  decoder de software do AOSP → planos pela CPU. É o modo
//                   seguro depois de um crash nativo numa etapa de vídeo.
//
//  Cadeia por decoder, só para a frente: DriverGl → CpuPlanes → SoftwarePlanes.
//
//  Cor: o sampler externo aplica a matriz e a faixa que o codec gravou no
//  buffer (BT.601/709, limitada/cheia) e entrega R'G'B' NÃO linear; o shader do
//  renderer segue com a curva de transferência, as primárias e o tone map do
//  arquivo (sampling.w = 1: sem matriz YCbCr). HDR de 10 bits chega em 8 bits
//  por canal (a cor fica certa; a precisão é a de SDR).
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/media/DecodedPlaneBounds.hpp"

#include <cstdint>

namespace aurea::android {

enum class VideoPath : unsigned char {
    DriverGl = 0,
    CpuPlanes = 1,
    SoftwarePlanes = 2,
};

inline const char* video_path_name(VideoPath p) noexcept {
    switch (p) {
        case VideoPath::DriverGl: return "caminho GL do driver";
        case VideoPath::CpuPlanes: return "planos pela CPU";
        case VideoPath::SoftwarePlanes: return "decoder de software";
    }
    return "?";
}

struct VideoPathInputs {
    /// O backend importa AHardwareBuffer RGBA como imagem (Vulkan com
    /// VK_ANDROID_external_memory_android_hardware_buffer). O GLES não importa.
    bool rgbaImport = false;
    /// Crash nativo numa etapa de vídeo na sessão anterior (modo seguro).
    bool safeMode = false;
    /// Miniatura: pequena, na CPU sempre.
    bool thumbnail = false;
};

/// O caminho com que um decoder NOVO começa. Todas as marcas começam no GL do
/// driver (Samsung inclusive). CPU readback for thumbnails/proxies uses a
/// software decoder: some vendor YUV planes fault even with valid bounds.
inline VideoPath initial_video_path(const VideoPathInputs& in) noexcept {
    if (in.safeMode || in.thumbnail) return VideoPath::SoftwarePlanes;
    if (!in.rgbaImport) return VideoPath::CpuPlanes;
    return VideoPath::DriverGl;
}

/// O decoder falhou com `code` no caminho `current`: para onde ir (false =
/// não há para onde; devolve o erro). BudgetExceeded é contrapressão (todos os
/// quadros em uso), nunca motivo para trocar de caminho.
inline bool next_video_path(VideoPath current, Errc code, bool hardwareDecoder, bool softwareDecoderExists,
                            VideoPath& next) noexcept {
    if (code == Errc::Ok || code == Errc::BudgetExceeded || code == Errc::Cancelled) return false;
    switch (current) {
        case VideoPath::DriverGl:
            // Qualquer falha do GL (EGL, EGLImage recusado, formato) ou do codec
            // com saída PRIVATE: os planos da CPU com o MESMO decoder de hardware.
            if (code == Errc::UnsupportedFeature || code == Errc::UnsupportedFormat || code == Errc::InvalidState
                || code == Errc::Timeout || code == Errc::DecodeFailed || code == Errc::OutOfMemory) {
                next = VideoPath::CpuPlanes;
                return true;
            }
            return false;
        case VideoPath::CpuPlanes:
            // A regra de sempre (video_software_fallback, modo de planos).
            if (!hardwareDecoder || !softwareDecoderExists) return false;
            if (code == Errc::Timeout || code == Errc::DecodeFailed || code == Errc::UnsupportedFormat
                || code == Errc::UnsupportedFeature) {
                next = VideoPath::SoftwarePlanes;
                return true;
            }
            return false;
        case VideoPath::SoftwarePlanes:
            return false;
    }
    return false;
}

/// O quadrilátero do passe GL: região visível do buffer do decoder → alvo RGBA.
struct ExternalQuad {
    /// Cantos da região visível em uv do buffer (u0,v0 = topo-esquerda na
    /// memória; v cresce para BAIXO, a ordem das linhas do AHardwareBuffer).
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    /// Caixa de clamp das amostras: onde o recorte termina DENTRO do buffer
    /// (sobra de alinhamento), um texel para dentro — o filtro e a croma não
    /// puxam a sobra; na borda do buffer, o centro do texel da borda.
    float minU = 0, minV = 0, maxU = 1, maxV = 1;
    /// Tamanho do alvo RGBA: a região visível, reduzida para caber em `maxSide`.
    uint32_t width = 0, height = 0;
};

inline bool external_quad(uint32_t bufW, uint32_t bufH, const media::VisibleRegion& vis, uint32_t maxSide,
                          ExternalQuad& out) noexcept {
    out = ExternalQuad{};
    if (!bufW || !bufH || !vis.width || !vis.height) return false;
    if (vis.left + vis.width > bufW || vis.top + vis.height > bufH) return false;
    const double W = bufW, H = bufH;
    const double l = vis.left, t = vis.top, r = double(vis.left) + vis.width, b = double(vis.top) + vis.height;
    out.u0 = float(l / W);
    out.v0 = float(t / H);
    out.u1 = float(r / W);
    out.v1 = float(b / H);
    double lo = (l + (vis.left > 0 ? 1.0 : 0.5)) / W;
    double hi = (r - (r < W ? 1.0 : 0.5)) / W;
    if (hi < lo) lo = hi = (l + r) * 0.5 / W;
    out.minU = float(lo);
    out.maxU = float(hi);
    lo = (t + (vis.top > 0 ? 1.0 : 0.5)) / H;
    hi = (b - (b < H ? 1.0 : 0.5)) / H;
    if (hi < lo) lo = hi = (t + b) * 0.5 / H;
    out.minV = float(lo);
    out.maxV = float(hi);
    uint32_t w = vis.width, h = vis.height;
    if (maxSide && (w > maxSide || h > maxSide)) {
        const double k = double(maxSide) / double(w > h ? w : h);
        w = uint32_t(double(w) * k + 0.5);
        h = uint32_t(double(h) * k + 0.5);
        w = w < 2 ? 2 : w;
        h = h < 2 ? 2 : h;
    }
    out.width = w;
    out.height = h;
    return true;
}

/// Quantos alvos RGBA um decoder no caminho GL pode ter vivos. O VideoSource
/// guarda `N - 5` no cache (sobram até 3 em voo na GPU, o quadro adiantado do
/// decoder e um de folga): cache de ~48 MB de RGBA, entre 1 e 7 quadros —
/// 1080p = 11 alvos, 4K = 6. Os alvos nascem sob demanda.
inline uint32_t driver_gl_live_frames(uint32_t width, uint32_t height) noexcept {
    const uint64_t bytes = uint64_t(width ? width : 1) * uint64_t(height ? height : 1) * 4;
    const uint64_t cache = (48ull << 20) / bytes;
    return 5u + (cache < 1 ? 1u : (cache > 7 ? 7u : uint32_t(cache)));
}

} // namespace aurea::android

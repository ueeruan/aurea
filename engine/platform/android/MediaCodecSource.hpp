// =============================================================================
//  Aurea / platform / android / MediaCodecSource.hpp
//
//  Decode de vídeo por hardware no Android: MediaExtractor + MediaCodec.
//
//  Dois caminhos de saída, decididos pelo que a GPU sabe fazer:
//
//   ZERO-COPY  MediaCodec → Surface do AImageReader (PRIVATE, GPU_SAMPLED)
//              → AImage → AHardwareBuffer → VkImage (conversão YCbCr externa).
//              Nenhum byte do frame passa pela CPU.
//
//   FALLBACK   MediaCodec → AImageReader YUV_420_888 → planos na CPU → upload.
//              O formato "flexível" do ImageReader normaliza os layouts de
//              fabricante (tiled, alinhado) que o getOutputBuffer cru exporia.
//
//  Origem do arquivo: "fd:<n>[:<offset>:<length>]", "content://…" (resolvido
//  por um callback da plataforma, que abre o descritor pelo ContentResolver)
//  ou um caminho comum.
// =============================================================================
#pragma once

#include "aurea/media/MediaManager.hpp"

#include <atomic>

namespace aurea::android {

/// Abre uma URI de conteúdo e devolve um descritor NOVO (o chamador fecha), ou -1.
using FdOpener = int (*)(const char* uri, void* ctx);

class MediaCodecFactory final : public VideoSourceFactory {
public:
    /// Zero-copy só quando o backend importa AHardwareBuffer com conversão
    /// YCbCr (GPUCapabilities::zero_copy_video). Vale para decoders abertos
    /// depois da chamada.
    void set_zero_copy(bool enabled) noexcept { zeroCopy_.store(enabled); }
    [[nodiscard]] bool zero_copy() const noexcept { return zeroCopy_.load(); }

    void set_fd_opener(FdOpener fn, void* ctx) noexcept {
        opener_ = fn;
        openerCtx_ = ctx;
    }

    [[nodiscard]] bool probe(const char* sourcePath, MediaProbe& out) override;
    [[nodiscard]] std::string cache_identity(const char* sourcePath) override;
    [[nodiscard]] std::unique_ptr<VideoDecoderBackend> open_video(const Asset& asset,
                                                                  MediaPriority priority) override;
    [[nodiscard]] std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char* sourcePath) override;

private:
    std::atomic<bool> zeroCopy_{true};
    FdOpener opener_ = nullptr;
    void* openerCtx_ = nullptr;
};

} // namespace aurea::android

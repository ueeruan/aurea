// =============================================================================
//  Aurea / export / AndroidVideoEncoder.hpp
//
//  Encoder de vídeo do aparelho (MediaCodec do NDK) com os dois jeitos de
//  alimentar que o Android oferece, usados como degraus da escada de abertura
//  do FFmpegExportSink:
//
//    Surface: o quadro NV12 do export vira RGB num contexto EGL próprio e é
//             desenhado na Surface de entrada do encoder, com o PTS exato
//             (eglPresentationTimeANDROID). É o caminho que a câmera do próprio
//             aparelho usa: o encoder escolhe o formato e o alinhamento dele —
//             nada de lista de formatos, stride ou altura alinhada a 16.
//    Buffer:  o NV12 é copiado no ByteBuffer de entrada no formato que o
//             ENCODER anuncia (CodecCapabilities.colorFormats) e com o layout
//             que ele devolve depois de configurado (image-data, stride,
//             slice-height) — nunca um layout presumido.
//
//  Modo de taxa, perfil e nível só entram no MediaFormat quando o encoder os
//  anuncia. O que sai são pacotes Annex-B; o SPS/PPS (VPS) chega à parte e o
//  sink usa como parâmetros globais do MP4/registro de pacotes.
//
//  Só Android (o resto do motor compila sem este arquivo fazer nada).
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/export/ExportSink.hpp"

#include <atomic>
#include <memory>
#include <vector>

namespace aurea::android_video {

enum class Input : u8 { Surface = 0, Buffer = 1 };

/// O que o encoder anuncia em MediaCodecInfo (consultado pela JVM).
struct CodecInfo {
    char name[128]{};
    bool hardware = false;
    bool surface = false;            ///< colorFormats tem COLOR_FormatSurface
    bool cbr = false, vbr = false;   ///< EncoderCapabilities.isBitrateModeSupported
    i32 baselineProfile = 0;         ///< AVC Baseline anunciado (0 = não)
    i32 baselineLevel = 0;
    std::vector<i32> colorFormats;
};

struct Request {
    bool hevc = false;
    u32 width = 0, height = 0;
    f64 fps = 30.0;
    u32 bitrateBps = 0;
    u32 rateMode = 1;            ///< 0 CBR, 1 VBR (só se o encoder anunciar)
    u32 keyframeFrames = 0;      ///< 0 = 2 s
    bool baseline = false;       ///< modo de segurança: Baseline se anunciado
    ExportColorTags color{};
    Input input = Input::Surface;
    /// Teste no aparelho (propriedade debug.aurea.enc.fail): o encoder abre mas
    /// nunca recebe quadro — simula o encoder que trava na partida.
    bool simulateStall = false;
    /// Teste: a thread da Surface "trava no driver" (eglSwapBuffers que não volta).
    bool simulateHang = false;
    /// Degrau surface-direct (só com Input::Surface): cada quadro chega como um
    /// AHardwareBuffer R'G'B' (set_direct_buffer), importado como EGLImage e
    /// desenhado na Surface — sem os planos NV12.
    bool direct = false;
    /// Batida do export (ExportWatchdog.hpp) durante as esperas com prazo.
    std::atomic<u64>* heartbeat = nullptr;
};

struct Packet {
    std::vector<u8> data;
    i64 ptsUs = 0;
    bool key = false;
};

#if defined(__ANDROID__)
/// Primeiro encoder de H.264/HEVC da lista do aparelho com a classificação
/// pedida (API 29: isHardwareAccelerated / isSoftwareOnly) que aceita o
/// tamanho e a taxa. false = nenhum, sem JVM ou API < 29.
[[nodiscard]] bool find_encoder(bool hevc, u32 width, u32 height, f64 fps, bool hardware, CodecInfo& out) noexcept;

class Encoder {
public:
    Encoder() noexcept;
    ~Encoder();
    Encoder(const Encoder&) = delete;
    Encoder& operator=(const Encoder&) = delete;

    [[nodiscard]] Status open(const Request& request, const CodecInfo& codec) noexcept;
    /// Um quadro NV12. Busy = o encoder não tem entrada livre agora (o sink
    /// drena a saída e tenta de novo, com prazo).
    /// Surface: espera o desenho voltar até `timeoutNs`; passado isso a
    /// Surface é dada como travada no driver (Timeout) e não é mais usada.
    [[nodiscard]] Status send(const u8* y, u32 yStride, const u8* uv, u32 uvStride, i64 ptsUs,
                              u64 timeoutNs = 5'000'000'000ull) noexcept;
    /// Fim do fluxo. Busy possível no modo Buffer (sem entrada livre).
    [[nodiscard]] Status end_of_stream() noexcept;
    /// Próximo pacote em até `timeoutUs`. Busy = nada ainda; `eos` = o encoder
    /// terminou (o pacote, se veio junto, está em `out` com `got`).
    [[nodiscard]] Status receive(Packet& out, bool& got, bool& eos, i64 timeoutUs) noexcept;
    /// SPS/PPS (VPS) Annex-B do BUFFER_FLAG_CODEC_CONFIG (vazio até chegar).
    [[nodiscard]] const std::vector<u8>& codec_config() const noexcept;
    /// Quadros entregues ainda sem pacote que o modo aceita (0 = sem teto).
    /// Surface: a fila do BufferQueue é pequena; passar dela travaria DENTRO
    /// do EGL se o encoder parar — o sink espera a saída antes.
    [[nodiscard]] u32 max_in_flight() const noexcept;
    /// "surface:c2.x.avc.encoder" / "buffer:c2.x.avc.encoder:0x15".
    [[nodiscard]] const char* describe() const noexcept;
    /// surface-direct: o AHardwareBuffer do PRÓXIMO send (o alimentador faz o
    /// próprio acquire e o segura até fechar).
    void set_direct_buffer(void* buffer) noexcept;
    /// Solta tudo. Um encoder travado no stop é abandonado depois de um prazo
    /// (thread própria) — o export segue no próximo degrau.
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Propriedades de teste (adb shell setprop): `debug.aurea.enc.allowsw` = 1
/// deixa os degraus do aparelho usarem encoder classificado como software
/// (emulador); `debug.aurea.enc.skip` = "surface,buffer,legacy" pula degraus;
/// `debug.aurea.enc.fail` = "surface-open,surface-stall,surface-hang,
/// surface-verify,buffer-open,buffer-stall,buffer-verify" força a falha de
/// cada degrau.
[[nodiscard]] bool debug_flag(const char* property, const char* token) noexcept;

#else
// Fora do Android não há encoder do aparelho por aqui (iOS usa o
// VideoToolbox pelo FFmpeg): a escada nunca cria um destes.
class Encoder {
public:
    [[nodiscard]] Status open(const Request&, const CodecInfo&) noexcept { return Errc::NotSupported; }
    [[nodiscard]] Status send(const u8*, u32, const u8*, u32, i64, u64 = 0) noexcept { return Errc::NotSupported; }
    [[nodiscard]] Status end_of_stream() noexcept { return Errc::NotSupported; }
    [[nodiscard]] Status receive(Packet&, bool& got, bool& eos, i64) noexcept { got = eos = false; return Errc::NotSupported; }
    [[nodiscard]] const std::vector<u8>& codec_config() const noexcept { return empty_; }
    [[nodiscard]] u32 max_in_flight() const noexcept { return 0; }
    [[nodiscard]] const char* describe() const noexcept { return ""; }
    void set_direct_buffer(void*) noexcept {}
    void close() noexcept {}
private:
    std::vector<u8> empty_;
};
[[nodiscard]] inline bool find_encoder(bool, u32, u32, f64, bool, CodecInfo& out) noexcept { out = CodecInfo{}; return false; }
[[nodiscard]] inline bool debug_flag(const char*, const char*) noexcept { return false; }
#endif

} // namespace aurea::android_video

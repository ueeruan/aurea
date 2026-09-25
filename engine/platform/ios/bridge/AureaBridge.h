// =============================================================================
//  Aurea / platform / ios / bridge / AureaBridge.h
//
//  A fronteira C++/ObjC++ do iOS. Este cabeçalho NÃO é visto pelo Swift: ele
//  fala `aurea::Engine`, `GPUBackend` e `Status`. A superfície que o Swift usa
//  é `AureaEngine.h`, que é ObjC puro e não inclui nada daqui.
//
//  Por que a separação existe: um cabeçalho com C++ dentro contamina tudo que o
//  inclui (o Swift Bridging Header compila como ObjC, não ObjC++), e um
//  cabeçalho ObjC não pode ser incluído numa unidade C++. Separando, cada lado
//  vê só o que entende — e é por isso que a ponte não precisa de `extern "C"`.
//
//  Quem é quem:
//    Host              o dono do motor: Engine + serviços de plataforma + ciclo
//                      de vida da superfície, como o NativeContext do Android.
//    Batch             o bloco de comandos de um frame (a CommandQueue é a
//                      ÚNICA forma de a UI mexer no motor — ver Command.hpp).
//    make_*            as fábricas de plataforma. Declaradas aqui (tipos do
//                      núcleo, sem nada de Apple) e definidas nos .mm:
//                        make_video_factory  → IOSVideoDecoder.mm (VideoToolbox)
//                        make_audio_output   → IOSAudio.mm (AVAudioEngine)
//                        make_export_sink    → IOSVideoDecoder.mm (VTCompression
//                                              + AVAssetWriter)
//                        fill_platform_info  → IOSVideoDecoder.mm
//                        ios_load_image      → IOSVideoDecoder.mm (ImageIO)
// =============================================================================
#pragma once

#ifdef __cplusplus

#include "aurea/Engine.hpp"
#include "aurea/audio/Audio.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/export/ExportSink.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/platform/DeviceCapabilities.hpp"

#include <memory>
#include <string>
#include <vector>

namespace aurea::ios {

// -----------------------------------------------------------------------------
// Serviços de plataforma
// -----------------------------------------------------------------------------

/// O que a ponte ajusta na fábrica de mídia depois de medir a GPU. O núcleo não
/// conhece isto: é contrato da plataforma, como o `MediaCodecFactory::
/// set_zero_copy` do Android.
class MediaFactoryControl {
public:
    virtual ~MediaFactoryControl() = default;
    /// Zero-copy = o `CVPixelBuffer` do decoder vira textura Metal sem passar
    /// pela CPU (`import_external_image`). Vale para decoders abertos depois.
    virtual void set_zero_copy(bool enabled) noexcept = 0;
};

/// Fábrica de mídia do iOS (VideoToolbox). `control`, quando não nulo, recebe o
/// ponto de ajuste do zero-copy.
[[nodiscard]] std::unique_ptr<VideoSourceFactory> make_video_factory(MediaFactoryControl** control);

/// Saída de som do iOS (AVAudioEngine + AVAudioSourceNode), ligada ao Audio
/// Engine COMPARTILHADO do núcleo — este arquivo não mixa nada, só entrega os
/// quadros que `audio::AudioEngine` já produziu (ver audio/Audio.hpp).
[[nodiscard]] std::unique_ptr<audio::AudioOutput> make_audio_output();

/// Encoder/contêiner do iOS: VTCompressionSession (H.264/HEVC) + AVAssetWriter.
/// É a MESMA fronteira `ExportSink` do MediaCodec/AMediaMuxer do Android.
[[nodiscard]] std::unique_ptr<ExportSink> make_export_sink(void* user);

/// Sonda o aparelho: núcleos, memória, e a tabela de codecs que o VideoToolbox
/// declara suportar. Os fatos que SÓ a plataforma sabe (DeviceCapabilities.hpp).
void fill_platform_info(PlatformInfo& out);

/// Imagem do projeto (JPEG/PNG/HEIC/…) em RGBA8 sRGB de alfa reto, pelo
/// ImageIO. Mesmo contrato do `decodeImage` do Android: é o que o motor chama
/// ao reabrir um projeto com imagens.
bool ios_load_image(const char* sourcePath, ImagePixels& out, void* ctx);

/// Fonte do sistema para o texto, se existir no aparelho. Vazio = o motor
/// procura sozinho (FontManager já varre /System/Library/Fonts).
[[nodiscard]] const char* ios_default_font_path();

// -----------------------------------------------------------------------------
// Batch — o bloco de comandos de um frame.
//
// A UI do editor mexe em dezenas de propriedades por frame durante um arrasto.
// Cada `set` aqui ACRESCENTA ao bloco; o `submit` atravessa a fronteira UMA vez
// (é o mesmo desenho do CommandBatch.kt do Android, e o mesmo motivo).
// -----------------------------------------------------------------------------
/// Texto de um comando: offset e comprimento no blob do lote. É o par que
/// `Command::stringOffset`/`stringLength` esperam — o motor copia exatamente
/// `length` bytes e NUL-termina do lado dele (ver Engine::submit_commands).
struct StringRef {
    u32 offset = 0;
    u32 length = 0;
    [[nodiscard]] bool valid() const noexcept { return length > 0; }
};

class Batch {
public:
    /// Comando vazio para o chamador preencher. Nulo = o bloco estourou o teto
    /// (trava contra laço infinito, não um limite de uso).
    Command* add(CommandType type) noexcept;

    /// Texto do próximo comando (nome de camada, conteúdo de texto, JSON).
    /// `offset == 0` só é ambíguo com "sem string" quando `length == 0`.
    StringRef add_string(const char* text) noexcept;

    /// Manda o bloco para o motor e o esvazia. Devolve quantos comandos entraram.
    u32 submit(Engine& engine) noexcept;

    void clear() noexcept { commands_.clear(); blob_.clear(); }
    [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }
    [[nodiscard]] usize size() const noexcept { return commands_.size(); }

    static constexpr usize kMaxCommands = 4096;
    static constexpr usize kMaxBlobBytes = 64 * 1024;

private:
    std::vector<Command> commands_;
    std::string          blob_;
};

// -----------------------------------------------------------------------------
// Host — o dono do motor no iOS.
//
// Espelha o `NativeContext` do Android (platform/android/aurea_jni.cpp), e a
// ordem de destruição é a MESMA e importa:
//
//   1. `shutdown()` do motor  — para a thread de render e devolve os decoders;
//   2. soltar a superfície    — o CAMetalLayer é da view, não nosso;
//   3. `~Host`                — o Engine morre ANTES do backend.
//
// O backend é criado pela ponte (`mtl::create_backend`) e a POSSE é do motor
// (`EngineConfig::backend`: "O motor assume a posse"). Por isso o Host guarda o
// `GPUBackend*` só para ligar o zero-copy — nunca para apagar.
// -----------------------------------------------------------------------------
class Host final {
public:
    Host() = default;
    ~Host();

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    /// Sobe o motor: dispositivo Metal, backend, renderer, thread de render.
    /// `nativeDevice` é o `id<MTLDevice>` do CAMetalLayer (nulo = o padrão do
    /// sistema). Sem superfície nesta etapa — ela chega no attach_surface, como
    /// no Android.
    [[nodiscard]] Status initialize(const std::string& cacheDirectory,
                                    const std::string& documentsDirectory,
                                    void* nativeDevice,
                                    f32 displayRefreshRate,
                                    bool debug) noexcept;

    void shutdown() noexcept;
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

    /// A superfície do iOS é um `CAMetalLayer*` (SurfaceDesc::nativeWindow).
    /// A posse do layer é da VIEW: aqui só se empresta o ponteiro.
    [[nodiscard]] bool attach_surface(void* metalLayer, u32 width, u32 height) noexcept;
    void detach_surface() noexcept;
    void resize_surface(u32 width, u32 height) noexcept;
    [[nodiscard]] bool has_surface() const noexcept { return hasSurface_; }

    /// Acorda a thread de render e FORÇA um quadro (algo mudou fora do modelo).
    void request_render() noexcept;
    /// Só acorda (redesenha se algo mudou). É o que o CADisplayLink chama.
    void wake_render() noexcept;
    /// A janela voltou a aparecer: reapresenta mesmo sem mudança no modelo.
    void invalidate() noexcept;

    [[nodiscard]] Engine* engine() noexcept { return engine_.get(); }
    /// Nulo quando não há GPU utilizável (o editor abre, sem preview).
    [[nodiscard]] GPUBackend* gpu() noexcept;
    /// Caminhos resolvidos para o motor — o Engine já os tem; a ponte precisa
    /// deles para o autosave e para o export (o arquivo de saída mora aqui).
    [[nodiscard]] const std::string& documents_directory() const noexcept { return documents_; }
    [[nodiscard]] const std::string& cache_directory() const noexcept { return cache_; }

    /// Motivo legível da última falha de `initialize` (a UI mostra; nunca um
    /// "não deu" sem dizer o quê).
    [[nodiscard]] const char* last_error() const noexcept { return lastError_.c_str(); }

private:
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<VideoSourceFactory> media_;
    MediaFactoryControl* mediaControl_ = nullptr;
    std::unique_ptr<audio::AudioOutput> audioOut_;
    std::string cache_;
    std::string documents_;
    std::string lastError_;
    bool initialized_ = false;
    bool hasSurface_ = false;
};

} // namespace aurea::ios

#endif // __cplusplus
